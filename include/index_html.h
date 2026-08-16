#pragma once
#include <Arduino.h>

// Single-page tank-drive UI. No LittleFS, no build step — embedded as a raw
// string literal in PROGMEM and served directly.
const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no, viewport-fit=cover">
<title>servo-remote</title>
<style>
  html, body {
    margin: 0; padding: 0; width: 100%; height: 100%;
    background: #0b0e12; color: #ddd;
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
    overflow: hidden;
    touch-action: none;
    -webkit-user-select: none; user-select: none;
    -webkit-touch-callout: none;
  }
  #status {
    position: fixed; top: 0; left: 0; right: 0; height: 44px;
    display: flex; align-items: center; justify-content: center;
    gap: 8px; font-size: 14px; letter-spacing: 0.02em;
    z-index: 10; background: #0b0e12;
  }
  #dot {
    width: 10px; height: 10px; border-radius: 50%;
    background: #833; transition: background 0.15s;
  }
  #dot.connected { background: #3a3; }
  .zone {
    position: fixed; top: 44px; bottom: 0; width: 50%;
    display: flex; align-items: flex-end; justify-content: center;
    box-sizing: border-box;
  }
  .zoneA { left: 0; border-right: 1px solid #222; }
  .zoneB { right: 0; }
  .track {
    position: relative; width: 62%; height: 92%; margin-bottom: 4%;
    background: #14181f; border-radius: 14px; overflow: hidden;
    border: 1px solid #232833;
  }
  .fill {
    position: absolute; left: 0; right: 0; bottom: 0; height: 0%;
    background: linear-gradient(#5ad, #38a);
    transition: none;
  }
  .pct {
    position: absolute; top: 10px; left: 0; right: 0;
    text-align: center; font-size: 15px; color: #889;
    pointer-events: none;
  }
  .label {
    position: absolute; bottom: 10px; left: 0; right: 0;
    text-align: center; font-size: 13px; color: #556; letter-spacing: 0.1em;
    pointer-events: none;
  }
</style>
</head>
<body>
  <div id="status"><span id="dot"></span><span id="statusText">connecting…</span></div>
  <div class="zone zoneA" id="zoneA">
    <div class="track" id="trackA">
      <div class="fill" id="fillA"></div>
      <div class="pct" id="pctA">0%</div>
      <div class="label">A</div>
    </div>
  </div>
  <div class="zone zoneB" id="zoneB">
    <div class="track" id="trackB">
      <div class="fill" id="fillB"></div>
      <div class="pct" id="pctB">0%</div>
      <div class="label">B</div>
    </div>
  </div>
<script>
(function () {
  "use strict";

  var throttle = { A: 0, B: 0 }; // 0..255, sent to the ESP32

  function makeZone(zoneId, trackId, fillId, pctId) {
    var zone = document.getElementById(zoneId);
    var track = document.getElementById(trackId);
    var fill = document.getElementById(fillId);
    var pct = document.getElementById(pctId);
    var key = zoneId === "zoneA" ? "A" : "B";
    var activePointerId = null;

    function valueFromY(clientY) {
      var rect = track.getBoundingClientRect();
      var frac = (rect.bottom - clientY) / rect.height;
      if (frac < 0) frac = 0;
      if (frac > 1) frac = 1;
      return Math.round(frac * 255);
    }

    function setValue(v) {
      throttle[key] = v;
      var frac = v / 255;
      fill.style.height = (frac * 100) + "%";
      pct.textContent = Math.round(frac * 100) + "%";
    }

    function onDown(e) {
      activePointerId = e.pointerId;
      zone.setPointerCapture(e.pointerId);
      setValue(valueFromY(e.clientY));
      requestWakeLock();
      e.preventDefault();
    }
    function onMove(e) {
      if (e.pointerId !== activePointerId) return;
      setValue(valueFromY(e.clientY));
      e.preventDefault();
    }
    function onUp(e) {
      if (e.pointerId !== activePointerId) return;
      activePointerId = null;
      setValue(0); // spring-return to zero, not centre — no reverse
      e.preventDefault();
    }

    zone.addEventListener("pointerdown", onDown);
    zone.addEventListener("pointermove", onMove);
    zone.addEventListener("pointerup", onUp);
    zone.addEventListener("pointercancel", onUp);
  }

  makeZone("zoneA", "trackA", "fillA", "pctA");
  makeZone("zoneB", "trackB", "fillB", "pctB");

  // Screen lock / tab backgrounding must NOT keep sending stale throttle —
  // the firmware failsafe handles the loss of packets, but drop to zero
  // locally too so a resumed page doesn't briefly resend an old value.
  document.addEventListener("visibilitychange", function () {
    if (document.hidden) {
      throttle.A = 0;
      throttle.B = 0;
    } else {
      requestWakeLock();
    }
  });

  // --- Wake Lock -----------------------------------------------------
  var wakeLock = null;
  function requestWakeLock() {
    if (!("wakeLock" in navigator)) return;
    navigator.wakeLock.request("screen").then(function (lock) {
      wakeLock = lock;
    }).catch(function () { /* denied or unsupported, ignore */ });
  }

  // --- WebSocket, ~25 Hz heartbeat -----------------------------------
  var ws = null;
  var dot = document.getElementById("dot");
  var statusText = document.getElementById("statusText");

  function setConnected(connected) {
    dot.className = connected ? "connected" : "";
    statusText.textContent = connected ? "connected" : "connecting…";
  }

  function connect() {
    ws = new WebSocket("ws://" + location.hostname + "/ws");
    ws.onopen = function () { setConnected(true); };
    ws.onclose = function () { setConnected(false); setTimeout(connect, 500); };
    ws.onerror = function () { ws.close(); };
  }
  connect();

  setInterval(function () {
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(throttle.A + "," + throttle.B);
    }
  }, 40); // ~25 Hz
})();
</script>
</body>
</html>
)HTMLPAGE";
