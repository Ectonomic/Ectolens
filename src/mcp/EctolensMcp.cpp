/* SPDX-FileCopyrightText: 2026 Ectolens contributors
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

/*
 * ectolens-mcp: Ectolens's MCP server (Model Context Protocol), for AI agents such as Claude Code.
 *
 * An agent starts it and speaks JSON-RPC on its standard input and output, one message a line
 * (the MCP stdio transport). It asks Ectolens over D-Bus (com.ectonomic.Ectolens, /Agent,
 * AgentService), and Ectolens asks the person: every capture and recording opens the overlay
 * framed on what the agent asked for, with private information blurred, and nothing is sent
 * until the person presses Send. Settings › Model Context Protocol (MCP) turns it on; until then every tool answers
 * that agents are off.
 *
 * Both MCP eras are spoken: the 2026-07-28 revision (every request carries its version in
 * _meta, server/discover), and the earlier ones that open with initialize.
 *
 * Tools: list_windows, capture, record_start, record_stop.
 */

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>

#include <unistd.h>

#include <cstdio>
#include <functional>

using namespace Qt::StringLiterals;

namespace
{
const QString s_modern = u"2026-07-28"_s;
const QStringList s_legacy = {u"2025-11-25"_s, u"2025-06-18"_s, u"2025-03-26"_s, u"2024-11-05"_s};
const QString s_metaVersion = u"io.modelcontextprotocol/protocolVersion"_s;
const QString s_metaClient = u"io.modelcontextprotocol/clientInfo"_s;
// how long a capture may wait for the person to send it
constexpr int s_waitForPersonMs = 15 * 60 * 1000;

const QString s_instructions = u"Ectolens takes pictures and recordings of this computer's screen, and the person decides what you get: "
                               "every capture and recording opens Ectolens's overlay framed on what you asked for, private information is "
                               "blurred, and nothing reaches you until they press Send. Give a short reason; they read it. Use "
                               "list_windows to find a window, capture to get a picture (return: text gives you its words instead of "
                               "pixels), and record_start and record_stop for a video file."_s;

void write(const QJsonObject &message)
{
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    fwrite(line.constData(), 1, size_t(line.size()), stdout);
    fflush(stdout);
}

QJsonObject textContent(const QString &text)
{
    return {{u"type"_s, u"text"_s}, {u"text"_s, text}};
}

QJsonObject targetSchema(bool withReturn)
{
    QJsonObject properties{
        {u"target"_s,
         QJsonObject{{u"type"_s, u"string"_s},
                     {u"enum"_s, QJsonArray{u"screen"_s, u"window"_s, u"active_window"_s, u"area"_s}},
                     {u"description"_s, u"What to frame: the whole screen the person is looking at, a window (give window), the active window, or an area (give x, y, width, height in screen coordinates)."_s}}},
        {u"window"_s, QJsonObject{{u"type"_s, u"string"_s}, {u"description"_s, u"For target window: an id from list_windows, or a name matched against the windows' apps and titles."_s}}},
        {u"x"_s, QJsonObject{{u"type"_s, u"integer"_s}}},
        {u"y"_s, QJsonObject{{u"type"_s, u"integer"_s}}},
        {u"width"_s, QJsonObject{{u"type"_s, u"integer"_s}, {u"minimum"_s, 1}}},
        {u"height"_s, QJsonObject{{u"type"_s, u"integer"_s}, {u"minimum"_s, 1}}},
        {u"reason"_s, QJsonObject{{u"type"_s, u"string"_s}, {u"maxLength"_s, 300}, {u"description"_s, u"Why you ask, in a few words the person reads before deciding."_s}}},
    };
    if (withReturn) {
        properties[u"return"_s] = QJsonObject{{u"type"_s, u"string"_s},
                                              {u"enum"_s, QJsonArray{u"image"_s, u"text"_s, u"image_and_text"_s}},
                                              {u"default"_s, u"image"_s},
                                              {u"description"_s, u"The picture, the words read from it (fewer tokens, for errors and logs), or both."_s}};
    }
    return {{u"type"_s, u"object"_s}, {u"properties"_s, properties}, {u"required"_s, QJsonArray{u"target"_s, u"reason"_s}}, {u"additionalProperties"_s, false}};
}

QJsonArray tools()
{
    return {
        QJsonObject{{u"name"_s, u"list_windows"_s},
                    {u"title"_s, u"List windows"_s},
                    {u"description"_s, u"The windows on the person's current desktop, topmost first: id, app, title (private words blurred), place and size, and which is active. Asks nothing of the person."_s},
                    {u"inputSchema"_s, QJsonObject{{u"type"_s, u"object"_s}, {u"additionalProperties"_s, false}}}},
        QJsonObject{{u"name"_s, u"capture"_s},
                    {u"title"_s, u"Capture the screen"_s},
                    {u"description"_s, u"A picture of the screen, a window or an area, which the person sees framed first, can adjust or mark up, and sends or refuses. Waits for them."_s},
                    {u"inputSchema"_s, targetSchema(true)}},
        QJsonObject{{u"name"_s, u"record_start"_s},
                    {u"title"_s, u"Start a screen recording"_s},
                    {u"description"_s, u"Starts recording the screen, a window or an area once the person agrees in Ectolens's overlay. Call record_stop to finish and get the file."_s},
                    {u"inputSchema"_s, targetSchema(false)}},
        QJsonObject{{u"name"_s, u"record_stop"_s},
                    {u"title"_s, u"Stop the screen recording"_s},
                    {u"description"_s, u"Stops the recording record_start began (or finds the one the person stopped) and gives its file's path."_s},
                    {u"inputSchema"_s, QJsonObject{{u"type"_s, u"object"_s}, {u"additionalProperties"_s, false}}}},
    };
}

class Server : public QObject
{
public:
    Server()
        : m_stdin(STDIN_FILENO, QSocketNotifier::Read)
    {
        connect(&m_stdin, &QSocketNotifier::activated, this, &Server::readInput);
    }

private:
    QSocketNotifier m_stdin;
    QByteArray m_buffer;
    QString m_client; // the agent's name, as its client says it

    void readInput()
    {
        char chunk[65536];
        const ssize_t n = ::read(STDIN_FILENO, chunk, sizeof chunk);
        if (n <= 0) {
            // the agent went away
            QCoreApplication::quit();
            return;
        }
        m_buffer.append(chunk, n);
        qsizetype end;
        while ((end = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(end).trimmed();
            m_buffer.remove(0, end + 1);
            if (!line.isEmpty()) {
                handle(line);
            }
        }
    }

    static void reply(const QJsonValue &id, const QJsonObject &result)
    {
        write({{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"result"_s, result}});
    }
    static void error(const QJsonValue &id, int code, const QString &message, const QJsonObject &data = {})
    {
        QJsonObject e{{u"code"_s, code}, {u"message"_s, message}};
        if (!data.isEmpty()) {
            e[u"data"_s] = data;
        }
        write({{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"error"_s, e}});
    }

    void handle(const QByteArray &line)
    {
        QJsonParseError parse;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parse);
        if (parse.error != QJsonParseError::NoError || !doc.isObject()) {
            error(QJsonValue::Null, -32700, u"Parse error"_s);
            return;
        }
        const QJsonObject message = doc.object();
        const QString method = message.value(u"method"_s).toString();
        const QJsonValue id = message.value(u"id"_s);
        const QJsonObject params = message.value(u"params"_s).toObject();
        if (!message.contains(u"id"_s)) {
            return; // a notification (initialized, cancelled): nothing to answer
        }
        // the modern era: every request names its version in _meta
        const QJsonObject meta = params.value(u"_meta"_s).toObject();
        const bool modern = meta.contains(s_metaVersion);
        if (modern) {
            const QString version = meta.value(s_metaVersion).toString();
            if (version != s_modern) {
                QJsonArray supported{s_modern};
                for (const auto &v : s_legacy) {
                    supported.append(v);
                }
                error(id, -32022, u"Unsupported protocol version"_s, {{u"supported"_s, supported}, {u"requested"_s, version}});
                return;
            }
            const QJsonObject info = meta.value(s_metaClient).toObject();
            const QString name = info.value(u"title"_s).toString(info.value(u"name"_s).toString());
            if (!name.isEmpty()) {
                m_client = name;
            }
        }
        auto done = [modern, id](QJsonObject result) {
            if (modern) {
                result[u"resultType"_s] = u"complete"_s;
            }
            reply(id, result);
        };
        const QJsonObject serverInfo{{u"name"_s, u"ectolens"_s}, {u"title"_s, u"Ectolens"_s}, {u"version"_s, QCoreApplication::applicationVersion()}};
        if (method == u"initialize"_s) {
            const QString asked = params.value(u"protocolVersion"_s).toString();
            m_client = params.value(u"clientInfo"_s).toObject().value(u"title"_s).toString(
                params.value(u"clientInfo"_s).toObject().value(u"name"_s).toString());
            reply(id,
                  {{u"protocolVersion"_s, s_legacy.contains(asked) ? asked : s_legacy.first()},
                   {u"capabilities"_s, QJsonObject{{u"tools"_s, QJsonObject{}}}},
                   {u"serverInfo"_s, serverInfo},
                   {u"instructions"_s, s_instructions}});
        } else if (method == u"server/discover"_s) {
            QJsonArray supported{s_modern};
            for (const auto &v : s_legacy) {
                supported.append(v);
            }
            done({{u"supportedVersions"_s, supported},
                  {u"capabilities"_s, QJsonObject{{u"tools"_s, QJsonObject{}}}},
                  {u"_meta"_s, QJsonObject{{u"io.modelcontextprotocol/serverInfo"_s, serverInfo}}},
                  {u"instructions"_s, s_instructions}});
        } else if (method == u"ping"_s) {
            done({});
        } else if (method == u"tools/list"_s) {
            done({{u"tools"_s, tools()}});
        } else if (method == u"tools/call"_s) {
            call(params.value(u"name"_s).toString(), params.value(u"arguments"_s).toObject(), id, done);
        } else {
            error(id, -32601, u"Method not found: "_s + method);
        }
    }

    using Done = std::function<void(QJsonObject)>;

    static QJsonObject toolError(const QString &text)
    {
        return {{u"content"_s, QJsonArray{textContent(text)}}, {u"isError"_s, true}};
    }

    // what a status other than success means to the agent
    static QString statusWords(const QJsonObject &r)
    {
        const QString status = r.value(u"status"_s).toString();
        if (status == u"off"_s) {
            return u"Agents are off in Ectolens. The person can turn them on in Ectolens's Settings, under Model Context Protocol (MCP)."_s;
        }
        if (status == u"declined"_s) {
            return u"The person chose not to send it."_s;
        }
        if (status == u"busy"_s) {
            return u"Ectolens is busy: "_s + r.value(u"message"_s).toString(u"another capture or recording is open."_s) + u" Try again later."_s;
        }
        if (status == u"not_recording"_s) {
            return u"No recording is running. If the person stopped one, it was saved with their recordings."_s;
        }
        return r.value(u"message"_s).toString(u"Ectolens answered: "_s + status);
    }

    // ask Ectolens, and hand its JSON answer on (or say why it couldn't be asked)
    void ask(const QString &method, const QVariantList &args, std::function<void(const QJsonObject &)> then, Done done)
    {
        // a plain method call, made only when a tool is called: a QDBusInterface introspects Ectolens
        // when it is made, and that alone starts Ectolens over D-Bus, so every agent session that
        // started this server started Ectolens too (2026-11-04; KDE's D544 review names the
        // constructor's introspection and this fix). The call itself still starts Ectolens.
        auto message = QDBusMessage::createMethodCall(u"com.ectonomic.Ectolens"_s, u"/Agent"_s, u"com.ectonomic.Ectolens.Agent"_s, method);
        message.setArguments(args);
        auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, s_waitForPersonMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, then, done] {
            watcher->deleteLater();
            QDBusPendingReply<QString> answer = *watcher;
            if (answer.isError()) {
                done(toolError(u"Ectolens could not be reached on D-Bus: "_s + answer.error().message()));
                return;
            }
            then(QJsonDocument::fromJson(answer.value().toUtf8()).object());
        });
    }

    void call(const QString &name, const QJsonObject &args, const QJsonValue &id, Done done)
    {
        if (name == u"list_windows"_s) {
            ask(u"Windows"_s, {}, [done](const QJsonObject &r) {
                if (r.value(u"status"_s).toString() != u"ok"_s) {
                    done(toolError(statusWords(r)));
                    return;
                }
                const QJsonArray windows = r.value(u"windows"_s).toArray();
                QStringList lines;
                for (const auto &w : windows) {
                    const QJsonObject o = w.toObject();
                    lines << u"%1  %2 · %3  %4×%5 at %6,%7%8"_s.arg(o.value(u"id"_s).toString(), o.value(u"app"_s).toString(), o.value(u"title"_s).toString())
                                 .arg(o.value(u"width"_s).toInt()).arg(o.value(u"height"_s).toInt()).arg(o.value(u"x"_s).toInt()).arg(o.value(u"y"_s).toInt())
                                 .arg(o.value(u"active"_s).toBool() ? u"  (active)"_s : QString());
                }
                done({{u"content"_s, QJsonArray{textContent(lines.isEmpty() ? u"No windows."_s : lines.join(u'\n'))}},
                      {u"structuredContent"_s, QJsonObject{{u"windows"_s, windows}}}});
            }, done);
        } else if (name == u"capture"_s || name == u"record_start"_s) {
            const QString target = args.value(u"target"_s).toString();
            if (!QStringList{u"screen"_s, u"window"_s, u"active_window"_s, u"area"_s}.contains(target)) {
                done(toolError(u"target must be screen, window, active_window or area."_s));
                return;
            }
            QJsonObject request{{u"target"_s, target}, {u"reason"_s, args.value(u"reason"_s).toString()}, {u"client"_s, m_client}};
            const QString returns = args.value(u"return"_s).toString(u"image"_s);
            request[u"text"_s] = returns != u"image"_s;
            if (target == u"area"_s) {
                if (args.value(u"width"_s).toInt() <= 0 || args.value(u"height"_s).toInt() <= 0) {
                    done(toolError(u"An area needs x, y, width and height."_s));
                    return;
                }
                request[u"area"_s] = QJsonObject{{u"x"_s, args.value(u"x"_s).toInt()}, {u"y"_s, args.value(u"y"_s).toInt()},
                                                 {u"width"_s, args.value(u"width"_s).toInt()}, {u"height"_s, args.value(u"height"_s).toInt()}};
            }
            const bool record = name == u"record_start"_s;
            auto start = [this, record, returns, done](QJsonObject r) {
                ask(record ? u"RecordStart"_s : u"Capture"_s, {QString::fromUtf8(QJsonDocument(r).toJson(QJsonDocument::Compact))},
                    [record, returns, done](const QJsonObject &answer) {
                        const QString status = answer.value(u"status"_s).toString();
                        if (record) {
                            if (status == u"recording"_s) {
                                done({{u"content"_s, QJsonArray{textContent(u"Recording started. Call record_stop to finish and get the file."_s)}}});
                            } else {
                                done(toolError(statusWords(answer)));
                            }
                            return;
                        }
                        if (status != u"sent"_s) {
                            done(toolError(statusWords(answer)));
                            return;
                        }
                        const QString path = answer.value(u"path"_s).toString();
                        QJsonArray content;
                        if (returns != u"text"_s) {
                            QFile file(path);
                            if (!file.open(QIODevice::ReadOnly)) {
                                done(toolError(u"The picture was sent but could not be read at "_s + path));
                                return;
                            }
                            content.append(QJsonObject{{u"type"_s, u"image"_s}, {u"data"_s, QString::fromLatin1(file.readAll().toBase64())}, {u"mimeType"_s, u"image/png"_s}});
                        }
                        if (returns != u"image"_s) {
                            content.append(textContent(answer.value(u"text"_s).toString()));
                        }
                        content.append(textContent(u"The person sent a %1×%2 picture, saved at %3."_s.arg(answer.value(u"width"_s).toInt())
                                                       .arg(answer.value(u"height"_s).toInt()).arg(path)));
                        done({{u"content"_s, content}});
                    }, done);
            };
            if (target != u"window"_s) {
                start(request);
                return;
            }
            // a window by its id, or by a name matched against the list
            const QString wanted = args.value(u"window"_s).toString().trimmed();
            if (wanted.isEmpty()) {
                done(toolError(u"For target window, give window: an id from list_windows, or a name."_s));
                return;
            }
            ask(u"Windows"_s, {}, [wanted, request, start, done](const QJsonObject &r) mutable {
                if (r.value(u"status"_s).toString() != u"ok"_s) {
                    done(toolError(statusWords(r)));
                    return;
                }
                QJsonArray matches;
                for (const auto &w : r.value(u"windows"_s).toArray()) {
                    const QJsonObject o = w.toObject();
                    if (o.value(u"id"_s).toString() == wanted) {
                        matches = {o};
                        break;
                    }
                    if (o.value(u"app"_s).toString().contains(wanted, Qt::CaseInsensitive) || o.value(u"title"_s).toString().contains(wanted, Qt::CaseInsensitive)) {
                        matches.append(o);
                    }
                }
                if (matches.size() != 1) {
                    QStringList names;
                    for (const auto &m : matches) {
                        names << m.toObject().value(u"id"_s).toString() + u"  "_s + m.toObject().value(u"app"_s).toString() + u" · "_s + m.toObject().value(u"title"_s).toString();
                    }
                    done(toolError(matches.isEmpty() ? u"No window matches \"%1\". list_windows shows them."_s.arg(wanted)
                                                     : u"%1 windows match \"%2\"; give one's id:\n%3"_s.arg(matches.size()).arg(wanted, names.join(u'\n'))));
                    return;
                }
                request[u"window"_s] = matches.first().toObject().value(u"id"_s).toString();
                start(request);
            }, done);
        } else if (name == u"record_stop"_s) {
            ask(u"RecordStop"_s, {}, [done](const QJsonObject &r) {
                if (r.value(u"status"_s).toString() == u"saved"_s) {
                    done({{u"content"_s, QJsonArray{textContent(u"The recording is saved at "_s + r.value(u"path"_s).toString())}}});
                } else {
                    done(toolError(statusWords(r)));
                }
            }, done);
        } else {
            error(id, -32602, u"Unknown tool: "_s + name);
        }
    }
};
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"ectolens-mcp"_s);
    QCoreApplication::setApplicationVersion(QStringLiteral(ECTOLENS_VERSION));
    Server server;
    return app.exec();
}
