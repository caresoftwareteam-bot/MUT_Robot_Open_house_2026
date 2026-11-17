"use strict";
const $ = (id) => document.getElementById(id);
let ws = null,
  activePointer = null,
  sendTimer = null,
  vector = { x: 0, y: 0 };
const clamp = (v, a, b) => Math.max(a, Math.min(b, v));
function isConnected() {
  return ws && ws.readyState === WebSocket.OPEN;
}
function status(msg, online = false) {
  $("status").textContent = msg;
  $("status").classList.toggle("online", online);
  $("connect").textContent = online ? "Disconnect" : "Connect Robot";
  $("connect").classList.toggle("connected", online);
}
function payload() {
  const speed = Number($("speed").value) / 100;

  return `[${Math.round(vector.x * speed)},${Math.round(vector.y * speed)}]`;
}
function transmit() {
  if (!isConnected()) return;

  const data = payload();
  console.log("[WS TX]", data);
  ws.send(data);
}
function setVector(x, y) {
  vector = {
    x: Math.round(clamp(x, -100, 100)),
    y: Math.round(clamp(y, -100, 100)),
  };
  $("coords").textContent = `[X:${vector.x}, Y:${vector.y}]`;
  console.log(`[UI] X:${vector.x} Y:${vector.y} -> [TX] ${payload()}`);
  transmit();
}
function stop() {
  setVector(0, 0);
  $("knob").style.left = "50%";
  $("knob").style.top = "50%";
  document
    .querySelectorAll(".dir")
    .forEach((b) => b.classList.remove("active"));
  clearInterval(sendTimer);
  sendTimer = null;
}
function startRepeating() {
  clearInterval(sendTimer);
  sendTimer = setInterval(transmit, 100);
}
$("connect").onclick = () => {
  if (ws) {
    const old = ws;
    ws = null;
    stop();
    old.close();
    status("Disconnected");
    return;
  }
  const ip = $("ip").value.trim(),
    port = Number($("port").value);
  if (!ip || !Number.isInteger(port) || port < 1 || port > 65535) {
    status("Invalid IP/port");
    return;
  }
  try {
    status("Connecting...");
    const socket = new WebSocket(`ws://${ip}:${port}/`);
    ws = socket;
    socket.onopen = () => {
      if (ws === socket) status("Connected", true);
    };
    socket.onmessage = (e) => {
      if (ws === socket && e.data === "BUSY") {
        stop();
        status("Robot busy");
        socket.close();
      }
    };
    socket.onclose = () => {
      if (ws === socket) {
        ws = null;
        stop();
        status("Disconnected");
      }
    };
    socket.onerror = () => {
      if (ws === socket) status("Connection error");
    };
  } catch (e) {
    ws = null;
    status("Invalid WebSocket address");
  }
};
$("speed").oninput = () => {
  $("speedValue").textContent = $("speed").value;
  transmit();
};
document.querySelectorAll(".dir").forEach((b) => {
  b.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    if (activePointer !== null) return;
    activePointer = e.pointerId;
    b.setPointerCapture(e.pointerId);
    b.classList.add("active");
    setVector(Number(b.dataset.x), Number(b.dataset.y));
    startRepeating();
  });
  const release = (e) => {
    if (activePointer !== e.pointerId) return;
    activePointer = null;
    stop();
  };
  b.addEventListener("pointerup", release);
  b.addEventListener("pointercancel", release);
  b.addEventListener("lostpointercapture", release);
});
const joy = $("joy"),
  knob = $("knob");
function moveJoy(e) {
  const r = joy.getBoundingClientRect(),
    cx = r.left + r.width / 2,
    cy = r.top + r.height / 2,
    max = r.width / 2 - knob.offsetWidth / 2 - 8,
    dx = e.clientX - cx,
    dy = e.clientY - cy,
    d = Math.hypot(dx, dy),
    s = d > max ? max / d : 1,
    px = dx * s,
    py = dy * s;
  knob.style.left = `calc(50% + ${px}px)`;
  knob.style.top = `calc(50% + ${py}px)`;
  setVector((-py / max) * 100, (px / max) * 100);
}
joy.addEventListener("pointerdown", (e) => {
  if (activePointer !== null) return;
  e.preventDefault();
  activePointer = e.pointerId;
  joy.setPointerCapture(e.pointerId);
  moveJoy(e);
  startRepeating();
});
joy.addEventListener("pointermove", (e) => {
  if (activePointer === e.pointerId) moveJoy(e);
});
const releaseJoy = (e) => {
  if (activePointer !== e.pointerId) return;
  activePointer = null;
  stop();
};
joy.addEventListener("pointerup", releaseJoy);
joy.addEventListener("pointercancel", releaseJoy);
joy.addEventListener("lostpointercapture", releaseJoy);
window.addEventListener("blur", () => {
  activePointer = null;
  stop();
});
document.addEventListener("visibilitychange", () => {
  if (document.hidden) {
    activePointer = null;
    stop();
  }
});
window.addEventListener("beforeunload", () => {
  if (isConnected()) ws.send("[0,0]");
});
