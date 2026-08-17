// SPDX-FileCopyrightText: Copyright (C) Arduino s.r.l. and/or its affiliated companies
// SPDX-License-Identifier: MPL-2.0

// WebUI Initialization
const ui = new WebUI();
const errorContainer = document.getElementById('error-container');

ui.on_connect(onUIConnected);
ui.on_disconnect(onUIDisconnected);
ui.on_message('dms_telemetry', handleTelemetry);
ui.on_message('frame_boxes', handleFrameBoxes);
ui.on_message('detection', handleDetectionLog);

// -------------------------------------------------------------
// 1. TAB SWITCHING LOGIC
// -------------------------------------------------------------
document.querySelectorAll('.nav-tab').forEach(tabBtn => {
  tabBtn.addEventListener('click', () => {
    document.querySelectorAll('.nav-tab').forEach(b => b.classList.remove('active'));
    document.querySelectorAll('.tab-page').forEach(p => p.classList.remove('active'));

    tabBtn.classList.add('active');
    const targetTabId = tabBtn.dataset.tab;
    document.getElementById(targetTabId).classList.add('active');

    if (targetTabId === 'tab-camera') {
      resizeCanvas();
    }
  });
});

// -------------------------------------------------------------
// 2. LIVE REAL-TIME CLOCK (HH:MM:SS & DATE)
// -------------------------------------------------------------
function updateClock() {
  const now = new Date();
  const timeStr = now.toTimeString().split(' ')[0]; // HH:MM:SS
  const days = ['SUN', 'MON', 'TUE', 'WED', 'THU', 'FRI', 'SAT'];
  const months = ['JAN', 'FEB', 'MAR', 'APR', 'MAY', 'JUN', 'JUL', 'AUG', 'SEP', 'OCT', 'NOV', 'DEC'];
  
  const dayStr = days[now.getDay()];
  const monthStr = months[now.getMonth()];
  const dateNum = String(now.getDate()).padStart(2, '0');
  const dateStr = `${dayStr} · ${monthStr} ${dateNum}`;

  const clockTimeEl = document.getElementById('clockTime');
  const clockDateEl = document.getElementById('clockDate');
  if (clockTimeEl) clockTimeEl.textContent = timeStr;
  if (clockDateEl) clockDateEl.textContent = dateStr;
}
setInterval(updateClock, 1000);
updateClock();

// Decorative Stars
const starsEl = document.getElementById('stars');
if (starsEl) {
  for (let i = 0; i < 40; i++) {
    const s = document.createElement('span');
    s.style.left = Math.random() * 100 + '%';
    s.style.top = Math.random() * 55 + '%';
    s.style.opacity = (0.25 + Math.random() * 0.6).toFixed(2);
    starsEl.appendChild(s);
  }
}

// -------------------------------------------------------------
// 3. SPEEDOMETER GAUGE GEOMETRY
// -------------------------------------------------------------
const ticksG = document.getElementById('ticks');
const arcTrackEl = document.getElementById('arcTrack');
const arcFillEl = document.getElementById('arcFill');
const cx = 200, cy = 200, rOuter = 160, rInner = 142, rLabel = 118;
const maxSpeed = 160, startDeg = 135, sweepDeg = 270;

function toXY(deg, r) {
  const rad = (deg * Math.PI) / 180;
  return [cx + r * Math.cos(rad), cy + r * Math.sin(rad)];
}

function arcPath(fromDeg, toDeg, r) {
  const [x1, y1] = toXY(fromDeg, r);
  const [x2, y2] = toXY(toDeg, r);
  const large = (toDeg - fromDeg) % 360 > 180 ? 1 : 0;
  return `M ${x1} ${y1} A ${r} ${r} 0 ${large} 1 ${x2} ${y2}`;
}

if (arcTrackEl && arcFillEl && ticksG) {
  arcTrackEl.setAttribute('d', arcPath(startDeg, startDeg + sweepDeg, rOuter));
  arcFillEl.setAttribute('d', arcPath(startDeg, startDeg + sweepDeg, rOuter));
  const arcTotalLen = arcTrackEl.getTotalLength();

  for (let v = 0; v <= maxSpeed; v += 20) {
    const deg = startDeg + (v / maxSpeed) * sweepDeg;
    const [x1, y1] = toXY(deg, rOuter);
    const [x2, y2] = toXY(deg, rInner);
    const major = (v % 40 === 0);

    const line = document.createElementNS('http://www.w3.org/2000/svg', 'line');
    line.setAttribute('x1', x1); line.setAttribute('y1', y1);
    line.setAttribute('x2', x2); line.setAttribute('y2', y2);
    line.setAttribute('class', 'tick' + (major ? ' major' : ''));
    ticksG.appendChild(line);

    if (major) {
      const [lx, ly] = toXY(deg, rLabel);
      const t = document.createElementNS('http://www.w3.org/2000/svg', 'text');
      t.setAttribute('x', lx); t.setAttribute('y', ly + 7);
      t.setAttribute('class', 'tick-label');
      t.textContent = v;
      ticksG.appendChild(t);
    }
  }

  window.setSpeed = function(v) {
    v = Math.max(0, Math.min(maxSpeed, v));
    const speedNumEl = document.getElementById('speedNum');
    if (speedNumEl) speedNumEl.textContent = v;

    const pct = v / maxSpeed;
    const needleDeg = startDeg + pct * sweepDeg - 270;
    const needleEl = document.getElementById('needle');
    if (needleEl) needleEl.style.setProperty('--needle-angle', needleDeg + 'deg');

    const fillLen = arcTotalLen * pct;
    arcFillEl.setAttribute('stroke-dasharray', fillLen + ' ' + arcTotalLen);
  };
  window.setSpeed(0);
}

// -------------------------------------------------------------
// 4. MCU TELEMETRY & ALERT STATE MACHINE (TOUCH DISMISS)
// -------------------------------------------------------------
function setLevel(level) {
  document.body.classList.remove('level-l1', 'level-l2', 'level-l3');
  const statusText = document.getElementById('statusText');

  if (level === 'l1') {
    document.body.classList.add('level-l1');
    if (statusText) statusText.textContent = 'ADVISORY';
  } else if (level === 'l2') {
    document.body.classList.add('level-l2');
    if (statusText) statusText.textContent = 'WARNING';
  } else if (level === 'l3') {
    document.body.classList.add('level-l3');
    if (statusText) statusText.textContent = 'DANGER';
  } else {
    if (statusText) statusText.textContent = 'MONITORING';
  }
}

// Chạm màn hình bất cứ lúc nào trên giao diện Cảnh báo để tắt còi và reset MCU
function dismissAlert() {
  ui.send_message('dismiss_alert', { action: 'dismiss' });
  setLevel('l0');
}

const alertLayer = document.getElementById('alertLayer');
if (alertLayer) {
  alertLayer.addEventListener('click', dismissAlert);
  alertLayer.addEventListener('touchstart', dismissAlert);
}

function handleTelemetry(telemetry) {
  if (!telemetry) return;

  // 1. Cập nhật tốc độ xe từ MCU
  if (telemetry.speed !== undefined && window.setSpeed) {
    window.setSpeed(telemetry.speed);
  }

  // 2. Chuyển đổi trạng thái cảnh báo từ MCU (0=Normal, 1=Advisory, 2=Warning, 3=Danger).
  // MCU (sketch.ino) giờ tính alertLevel bằng millis() thời gian thực và đã
  // tự phát đủ cả 4 mức (kể cả L3_DANGER), nên chỉ cần map thẳng 1-1 --
  // không suy luận lại từ eye_frames/yawn_frames ở đây nữa. Trước đây code
  // này tự đoán L3 qua "eyeFrames >= 5" (bù cho việc MCU cũ không bao giờ
  // tự phát L3) và chưa từng map alertLevel===3 tường minh; giờ camera fps
  // có thể thay đổi và một số frame có thể bị gộp/bỏ trước khi tới MCU
  // (xem main.py, _bridge_worker), nên eye_frames không còn tỉ lệ ổn định
  // với thời gian thực -- dùng nó để suy luận cảnh báo ở đây sẽ dễ sai lệch
  // so với quyết định thật của MCU.
  const alertLevel = telemetry.alert_level || 0;

  if (alertLevel === 3) {
    setLevel('l3'); // Danger modal
  } else if (alertLevel === 2) {
    setLevel('l2'); // Warning ring
  } else if (alertLevel === 1) {
    setLevel('l1'); // Advisory banner
  } else {
    setLevel('l0'); // Normal monitoring
  }
}

// Tiếp nhận sự kiện phản hồi đặc biệt từ MCU (Tài xế mở mắt trở lại sau khi nhắm lâu)
ui.on_message('dms_event', (eventData) => {
  if (eventData && eventData.type === 'EYES_REOPENED') {
    handleDetectionLog({
      content: `👁️ MỞ MẮT TRỞ LẠI (${eventData.closed_frames}f)`,
      accuracy: 1.0
    });
  }
});
// -------------------------------------------------------------
// 5. CAMERA IFRAME & CANVAS BOUNDING BOX RENDERER
// -------------------------------------------------------------
const iframe = document.getElementById('dynamicIframe');
const placeholder = document.getElementById('videoPlaceholder');
const canvas = document.getElementById('bboxCanvas');
const ctx = canvas ? canvas.getContext('2d') : null;

const currentHostname = window.location.hostname || 'localhost';
const targetPort = 4912;
const streamUrl = `http://${currentHostname}:${targetPort}/embed`;

let streamLoaded = false;
if (iframe) {
  iframe.src = streamUrl;
  iframe.onload = () => {
    if (placeholder) placeholder.style.display = 'none';
    iframe.style.display = 'block';
    streamLoaded = true;
    resizeCanvas();
  };
}

function resizeCanvas() {
  if (!canvas || !videoFeedContainer) return;
  canvas.width = videoFeedContainer.clientWidth;
  canvas.height = videoFeedContainer.clientHeight;
}
window.addEventListener('resize', resizeCanvas);

function handleFrameBoxes(data) {
  if (!data || !canvas || !ctx) return;

  const fpsValEl = document.getElementById('fpsVal');
  const detCountEl = document.getElementById('detCount');
  if (fpsValEl && data.fps !== undefined) fpsValEl.textContent = data.fps;
  if (detCountEl && data.boxes) detCountEl.textContent = data.boxes.length;

    if (data.boxes && data.boxes.length > 0) {
    data.boxes.forEach(b => {
      handleDetectionLog({ content: b.label, accuracy: b.confidence });
    });
  }

  resizeCanvas();
  ctx.clearRect(0, 0, canvas.width, canvas.height);

  if (!data.boxes || data.boxes.length === 0) return;

  const w = canvas.width;
  const h = canvas.height;

  data.boxes.forEach(box => {
    let bbox = box.bbox;
    let x1 = 0, y1 = 0, x2 = 0, y2 = 0;

    if (!bbox || !Array.isArray(bbox) || bbox.length !== 4) {
      return; // Không vẽ box mặc định nếu không có tọa độ thực
    }

    if (bbox[0] <= 1.0 && bbox[2] <= 1.0) {
      x1 = bbox[0] * w;
      y1 = bbox[1] * h;
      x2 = bbox[2] * w;
      y2 = bbox[3] * h;
    } else {
      x1 = bbox[0]; y1 = bbox[1]; x2 = bbox[2]; y2 = bbox[3];
    }


    const boxW = x2 - x1;
    const boxH = y2 - y1;

    // Chọn màu sắc theo class
    let strokeColor = '#3ddc84'; // Green for normal
    const labelLower = (box.label || '').toLowerCase();
    if (labelLower.includes('closed') || labelLower.includes('drowsy')) {
      strokeColor = '#ff3b3b'; // Red for closed eye
    } else if (labelLower.includes('yawn')) {
      strokeColor = '#ff8c2e'; // Orange for yawn
    }

    // Vẽ khung Bounding Box
    ctx.strokeStyle = strokeColor;
    ctx.lineWidth = 3;
    ctx.strokeRect(x1, y1, boxW, boxH);

    // Vẽ nhãn Label + Confidence
    const labelText = `${box.label}: ${(box.confidence * 100).toFixed(0)}%`;
    ctx.font = 'bold 14px sans-serif';
    const textWidth = ctx.measureText(labelText).width;

    ctx.fillStyle = strokeColor;
    ctx.fillRect(x1, Math.max(0, y1 - 24), textWidth + 12, 24);

    ctx.fillStyle = '#000';
    ctx.fillText(labelText, x1 + 6, Math.max(16, y1 - 7));
  });
}

function handleDetectionLog(det) {
  const logUl = document.getElementById('detectionLog');
  if (!logUl || !det) return;

  const li = document.createElement('li');
  const time = new Date().toLocaleTimeString();
  li.textContent = `[${time}] ${det.content} (${(det.accuracy * 100).toFixed(0)}%)`;
  logUl.insertBefore(li, logUl.firstChild);

  while (logUl.children.length > 8) {
    logUl.removeChild(logUl.lastChild);
  }
}

// Confidence Slider Handler
const confidenceSlider = document.getElementById('confidenceSlider');
const confidenceValueDisplay = document.getElementById('confidenceValueDisplay');
if (confidenceSlider) {
  confidenceSlider.addEventListener('input', e => {
    const val = parseFloat(e.target.value);
    if (confidenceValueDisplay) confidenceValueDisplay.textContent = val.toFixed(2);
    ui.send_message('override_th', val);
  });
}

function onUIConnected() {
  if (errorContainer) errorContainer.style.display = 'none';
}

function onUIDisconnected() {
  if (errorContainer) {
    errorContainer.textContent = 'Mất kết nối với Arduino MCU. Đang thử kết nối lại...';
    errorContainer.style.display = 'block';
  }
}
