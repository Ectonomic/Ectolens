# Stand-in sound for the tests, sourced, never run: a private PipeWire and WirePlumber with a
# stand-in speaker playing 440 Hz and a stand-in microphone hearing 880 Hz, as the defaults.
# The caller has already made it private: XDG_RUNTIME_DIR and XDG_CONFIG_HOME are the test's
# own, the real session's runtime folder is out of reach, and /dev/snd is hidden (bwrap
# --tmpfs /dev/snd), so no real speaker or microphone can be reached or changed.
#
#   start_standin_sound LOGDIR WORKDIR SECONDS
#     returns once both stand-ins are the defaults and their tones are playing (for SECONDS)
start_standin_sound() {
    local logs=$1 work=$2 secs=$3
    [ -z "$(ls /dev/snd 2>/dev/null)" ] || { echo "FAIL  sound devices are visible: refusing to start stand-in sound"; return 1; }
    case "$XDG_RUNTIME_DIR" in /run/user/*) echo "FAIL  XDG_RUNTIME_DIR is the real session's"; return 1;; esac
    mkdir -p "$XDG_CONFIG_HOME/wireplumber/wireplumber.conf.d"
    cat > "$XDG_CONFIG_HOME/wireplumber/wireplumber.conf.d/stand-ins-only.conf" <<'CONF'
wireplumber.profiles = {
  main = {
    monitor.alsa = disabled
    monitor.alsa-midi = disabled
    monitor.bluez = disabled
    monitor.bluez-midi = disabled
    monitor.v4l2 = disabled
    monitor.libcamera = disabled
  }
}
CONF
    pipewire >"$logs/pipewire.log" 2>&1 &
    for _ in $(seq 50); do [ -S "$XDG_RUNTIME_DIR/pipewire-0" ] && break; sleep 0.1; done
    wireplumber >"$logs/wireplumber.log" 2>&1 &
    sleep 1
    # the speaker: a null sink, whose monitor is the "system sound"; the microphone: a loopback,
    # what is played into its feed comes out of it as a source (PipeWire's own way to make one)
    pw-cli create-node adapter '{ factory.name=support.null-audio-sink node.name=standin-speakers node.description="Stand-in speakers" media.class=Audio/Sink audio.position=[FL FR] object.linger=true monitor.channel-volumes=true }' >/dev/null
    pw-loopback --capture-props='media.class=Audio/Sink node.name=standin-mic-feed node.description="Stand-in microphone feed" audio.position=[FL FR]' \
                --playback-props='media.class=Audio/Source node.name=standin-mic node.description="Stand-in microphone" audio.position=[FL FR]' >"$logs/loopback.log" 2>&1 &
    sleep 1
    pw-metadata 0 default.configured.audio.sink '{ "name": "standin-speakers" }' >/dev/null
    pw-metadata 0 default.configured.audio.source '{ "name": "standin-mic" }' >/dev/null
    sleep 0.5
    if ! pw-metadata 0 2>/dev/null | grep -q "default.audio.sink.*standin-speakers" || ! pw-metadata 0 2>/dev/null | grep -q "default.audio.source.*standin-mic"; then
        echo "FAIL  the stand-ins didn't become the defaults"; return 1
    fi
    if pw-cli ls Device 2>/dev/null | grep -q "api.alsa\|api.bluez"; then echo "FAIL  a real sound device showed up"; return 1; fi
    ffmpeg -v error -y -f lavfi -i "sine=frequency=440:duration=$secs:sample_rate=48000" -ac 2 "$work/440.wav"
    ffmpeg -v error -y -f lavfi -i "sine=frequency=880:duration=$secs:sample_rate=48000" -ac 2 "$work/880.wav"
    pw-play --target standin-speakers "$work/440.wav" &
    pw-play --target standin-mic-feed "$work/880.wav" &
    sleep 1
    echo "PASS  stand-in speakers (440 Hz) and microphone (880 Hz) are the defaults, and no sound card is visible"
}
