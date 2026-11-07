// app-match.js: show a mock's app frame the way a native KDE app looks on a KDE Plasma desktop.
//
// A native app lights its colors as KWin lights every ordinary app: a 2.2 curve. Chrome labels its
// pages as sRGB, and KWin lights those with sRGB's own curve, which is brighter in the darkest
// grays (the page's #18181b comes out 1.65 times as bright). Every color in the app's frame
// goes through a table that undoes the difference, so a hex value in a mock gives the light
// the same value gives in a native app. Nothing in Chrome or the app changes.
//
// Frames: elements with class "win" (the app's window), "lifted" (something being dragged) or
// "app-match". Add ?raw to a mock's address to see the page's own values, as a screenshot
// taken without the compositor shows them.
(() => {
  if (new URLSearchParams(location.search).has('raw')) return;
  // the value Chrome needs for the light a native app gives `v`: sRGB's encoding of v's 2.2 light
  const encode = l => (l <= 0.0031308 ? 12.92 * l : 1.055 * Math.pow(l, 1 / 2.4) - 0.055);
  // (Chrome rounds a table's results down to the step below, so each value is half a step up)
  const table = Array.from({ length: 256 }, (_, i) => Math.min(1, encode(Math.pow(i / 255, 2.2)) + 0.5 / 255).toFixed(6)).join(' ');
  const put = () => {
    const ns = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('width', '0');
    svg.setAttribute('height', '0');
    svg.setAttribute('aria-hidden', 'true');
    svg.style.position = 'absolute';
    svg.innerHTML = `<filter id="app-match" color-interpolation-filters="sRGB"><feComponentTransfer>`
      + ['R', 'G', 'B'].map(c => `<feFunc${c} type="table" tableValues="${table}"/>`).join('')
      + `</feComponentTransfer></filter>`;
    document.body.prepend(svg);
    const style = document.createElement('style');
    style.textContent = '.win, .lifted, .app-match { filter: url(#app-match); }';
    document.head.append(style);
  };
  if (document.body) put(); else document.addEventListener('DOMContentLoaded', put);
})();
