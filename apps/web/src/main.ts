import './style.css';

type ControlResult = {
  success: boolean;
  error?: string;
  duration_ms?: number;
  position_ms?: number;
  playing?: boolean;
  recording?: boolean;
  output_path?: string;
};

type SubtitleSegment = { start_ms: number; end_ms: number; text: string };
type EditSegment = { start_ms: number; end_ms: number; reason?: string };
type AgentMessage = {
  type: string;
  request_id?: string;
  session_id?: string;
  tool?: string;
  payload?: Record<string, unknown>;
  arguments?: Record<string, unknown>;
  error?: string;
};

const app = document.querySelector<HTMLDivElement>('#app')!;
app.innerHTML = `
<div class="app-shell">
  <header class="topbar">
    <div class="brand">
      <div class="brand-mark">A</div>
      <div class="brand-copy"><h1>AVPlayer</h1><span>AI MEDIA STUDIO</span></div>
    </div>
    <div class="connection-chip" id="header-status"><i></i><span>未连接</span></div>
  </header>

  <main class="workspace">
    <section class="stage">
      <div class="video-card">
        <video id="video" playsinline></video>
        <div class="video-empty" id="empty">
          <div class="empty-icon">▶</div>
          <strong>尚未加载媒体</strong>
          <span>在右侧上传视频并连接服务端</span>
        </div>
        <div class="subtitle" id="subtitle"></div>
        <div class="video-meta">
          <span class="pill" id="media-pill">NO MEDIA</span>
          <span id="resolution">—</span>
        </div>
        <div class="video-foot">
          <span id="play-state">已暂停</span>
          <span id="clock">00:00 / 00:00</span>
        </div>
      </div>

      <div class="progress-block">
        <input id="seek" type="range" min="0" max="1000" value="0">
        <div class="time-row"><span id="elapsed">00:00</span><span id="duration">00:00</span></div>
      </div>

      <div class="controller">
        <input id="file" type="file" accept="video/*" hidden>
        <button id="import">导入</button>
        <button id="play">播放</button>
        <button id="record">录制</button>
        <button id="screenshot">截图</button>
        <span class="spacer"></span>
        <button id="flip" class="toggle">垂直翻转</button>
        <button id="gray" class="toggle">黑白滤镜</button>
        <button id="invert" class="toggle">反相滤镜</button>
        <button id="sticker" class="toggle" disabled title="Web 端暂不支持动态贴纸">贴纸滤镜</button>
      </div>
    </section>

    <aside class="sidebar">
      <div class="tabs">
        <button class="tab active" data-tab="network">网络</button>
        <button class="tab" data-tab="ai">AI 助手</button>
        <button class="tab" data-tab="agent">Agent</button>
      </div>

      <section class="tab-page active" id="tab-network">
        <h2 class="panel-title">网络控制台</h2>
        <div class="field"><label>当前播放地址</label><div class="value blue" id="source">未加载</div></div>
        <div class="field"><label>连接状态</label><div class="value green" id="network-state">空闲</div></div>
        <div class="field"><label>最近错误</label><div class="error-box" id="error">暂无错误</div></div>

        <div class="field">
          <label>本地媒体</label>
          <div class="upload-row">
            <button id="upload">上传到服务端</button>
            <span id="file-name">未选择文件</span>
          </div>
        </div>
        <label for="session">会话 ID</label>
        <input id="session" value="web" readonly autocomplete="off" spellcheck="false">
        <label for="media">媒体 ID</label>
        <input id="media" placeholder="上传后自动填入" autocomplete="off" spellcheck="false">
        <div class="actions">
          <button id="connect" class="primary-green">连接并播放</button>
          <button id="stop" class="danger">停止</button>
        </div>
        <label>调试摘要</label>
        <pre id="debug">等待操作…</pre>
      </section>

      <section class="tab-page" id="tab-ai">
        <div class="panel-head">
          <div class="avatar ai">✦</div>
          <div><h2>AI 助手</h2><span>媒体理解与播放控制</span></div>
          <span class="chip" id="ai-config">未配置</span>
        </div>
        <div class="provider-row"><label>模型方式</label>
          <select id="ai-provider"><option value="local">本地模型</option><option value="cloud">豆包 API</option></select>
        </div>
        <div class="hint" id="ai-hint">本地模型：音频和字幕留在服务端。</div>

        <div class="context-card">
          <div class="context-head"><span class="section-label">当前媒体</span><span id="ai-record">未录制</span></div>
          <strong id="ai-source">尚未打开媒体</strong>
          <span id="ai-state">上传视频后，助手可以围绕当前媒体工作</span>
          <span id="ai-resolution">分辨率 —</span>
        </div>

        <div class="section-label">播放控制</div>
        <div class="two">
          <button id="ai-play" class="primary-blue">▶ 播放</button>
          <button id="ai-seek">↤ 回到开头</button>
        </div>
        <label class="check"><input id="subtitle-toggle" type="checkbox" checked> 显示字幕</label>

        <div class="section-label">AI 工具</div>
        <div class="tools">
          <button id="transcribe"><strong>字幕转写</strong><span>生成时间轴字幕</span></button>
          <button id="summary"><strong>视频摘要</strong><span>提炼章节与重点</span></button>
          <button id="search"><strong>语义搜索</strong><span>按内容查找片段</span></button>
          <button id="edit"><strong>智能剪辑</strong><span>选段并导出视频</span></button>
        </div>
        <label for="prompt">搜索 / 剪辑目标</label>
        <input id="prompt" placeholder="输入搜索问题或剪辑目标">
        <button id="export" class="wide">导出建议片段</button>
        <div class="section-label">处理结果</div>
        <pre id="ai-result" class="result">AI 处理结果将在这里显示</pre>
        <div class="status-line" id="ai-status">请上传媒体并等待 Agent 服务连接。</div>
      </section>

      <section class="tab-page" id="tab-agent">
        <div class="panel-head">
          <div class="avatar agent">◎</div>
          <div><h2>Agent 工作台</h2><span>目标拆解与工具编排</span></div>
          <span class="chip" id="agent-runtime">待接入</span>
        </div>
        <div class="provider-row"><label>模型方式</label>
          <select id="agent-provider"><option value="local">本地模型</option><option value="cloud">豆包 API</option></select>
        </div>
        <div class="hint">本地模型：任务内容留在服务端。</div>

        <div class="section-label">任务目标</div>
        <input id="goal" placeholder="例如：整理当前视频并给出摘要">
        <div class="two">
          <button id="run" class="primary-teal">开始执行</button>
          <button id="agent-stop">停止任务</button>
        </div>

        <div class="section-label">快捷目标</div>
        <div class="two">
          <button id="quick-summary" class="quick">整理视频内容</button>
          <button id="quick-search" class="quick">查找关键片段</button>
        </div>

        <div class="section-label">执行计划</div>
        <ul id="agent-plan" class="plan-list"><li class="muted">尚未执行</li></ul>
        <div class="section-label">工具调用</div>
        <div id="agent-tools" class="tool-list"><span class="muted">工具尚未调用</span></div>
        <div class="section-label">Agent 输出</div>
        <pre id="agent-output" class="result">任务完成后显示执行摘要与输出文件</pre>
        <div class="status-line" id="agent-status">Agent 尚未连接。</div>
      </section>
    </aside>
  </main>

  <footer><span>AVPLAYER · DISTRIBUTED MEDIA</span><span>WEBRTC · H.264 + OPUS</span></footer>
</div>`;

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;
const video = $<HTMLVideoElement>('video');
const seek = $<HTMLInputElement>('seek');
const mediaInput = $<HTMLInputElement>('media');
const sessionInput = $<HTMLInputElement>('session');
const providerSelects = [$<HTMLSelectElement>('ai-provider'), $<HTMLSelectElement>('agent-provider')];
const fileInput = $<HTMLInputElement>('file');

const WEB_SESSION = 'web';
let selectedFile: File | null = null;
let uploadedFile: File | null = null;
let uploading = false;
let mediaId = '';
let uploaded = false;
let connected = false;
let durationMs = 0;
let positionMs = 0;
let serverRecording = false;
let serverPlaying = false;
let subtitles: SubtitleSegment[] = [];
let planSegments: EditSegment[] = [];
let subtitleEnabled = true;
let hasTranscript = false;
let hasPlan = false;
let modelConfigured = false;
let speechConfigured = false;
let provider = 'local';
let agentBusy = false;
let agentWatchdog: number | undefined;
let intentionalAgentClose = false;
let agentRetry: number | undefined;
let statusTimer: number | undefined;
let peer: RTCPeerConnection | undefined;
let signalSocket: WebSocket | undefined;
let agentSocket: WebSocket | undefined;
let remoteStream = new MediaStream();
let iceServers: RTCIceServer[] = [{ urls: 'stun:stun.l.google.com:19302' }];
let flip = false;
let gray = false;
let invert = false;

const debugLines: string[] = [];
function log(message: string) {
  debugLines.push(message);
  while (debugLines.length > 10) debugLines.shift();
  $('debug').textContent = debugLines.join('\n');
}

function requestId(): string {
  return typeof crypto.randomUUID === 'function'
    ? crypto.randomUUID()
    : `${Date.now()}-${Math.random().toString(16).slice(2)}`;
}

function setError(message: string) {
  $('error').textContent = message || '暂无错误';
  if (message) log(message);
}

function downloadHref(path: string): string {
  const parts = path.replace(/\\/g, '/').split('/');
  let file = parts.pop() ?? '';
  if (parts[parts.length - 1] === 'clips') {
    parts.pop();
    file = `clips/${file}`;
  }
  const media = parts.pop() ?? '';
  const session = parts.pop() ?? '';
  return `/download?session_id=${encodeURIComponent(session)}&media_id=${encodeURIComponent(media)}&file=${encodeURIComponent(file)}`;
}

function renderResult(target: HTMLElement, text: string, paths: string[] = []): void {
  target.textContent = text;
  for (const path of paths) {
    const link = document.createElement('a');
    link.className = 'download-link';
    link.href = downloadHref(path);
    link.textContent = `下载 ${path.split('/').pop() ?? '文件'}`;
    target.appendChild(document.createElement('br'));
    target.appendChild(link);
  }
}

function artifactPaths(text: string): string[] {
  return Array.from(text.matchAll(/[^\s:：]+\.(?:srt|json|mp4)/g)).map((match) => match[0]);
}

function formatTime(ms: number): string {
  const total = Math.max(0, Math.floor(ms / 1000));
  return `${Math.floor(total / 60).toString().padStart(2, '0')}:${(total % 60).toString().padStart(2, '0')}`;
}

function setConnection(state: string, text: string) {
  const chip = $('header-status');
  chip.className = `connection-chip ${state}`;
  chip.innerHTML = `<i></i><span>${text}</span>`;
  $('network-state').textContent = text;
  $('network-state').className = `value ${state === 'connected' ? 'green' : state === 'error' ? 'red' : 'blue'}`;
}

function setPlayingUi(playing: boolean) {
  $('play').textContent = playing ? '暂停' : '播放';
  $('ai-play').textContent = playing ? 'Ⅱ 暂停' : '▶ 播放';
  $('play-state').textContent = playing ? '正在播放' : '已暂停';
}

function updateClock() {
  $('clock').textContent = `${formatTime(positionMs)} / ${formatTime(durationMs)}`;
  $('elapsed').textContent = formatTime(positionMs);
  $('duration').textContent = formatTime(durationMs);
}

function updateMediaContext() {
  const name = uploaded ? ($('file-name').textContent || mediaId) : '';
  $('source').textContent = name || (mediaId ? `web / ${mediaId}` : '未加载');
  $('ai-source').textContent = name || (mediaId ? `web / ${mediaId}` : '尚未打开媒体');
  $('ai-state').textContent = connected ? (serverPlaying ? '正在播放' : '已暂停') : '等待连接服务端';
  $('media-pill').textContent = mediaId ? 'REMOTE STREAM' : 'NO MEDIA';
  const width = video.videoWidth;
  const height = video.videoHeight;
  const resolution = width > 0 && height > 0 ? `${width} × ${height}` : '—';
  $('resolution').textContent = resolution;
  $('ai-resolution').textContent = `分辨率 ${resolution}`;
  $('ai-record').textContent = serverRecording ? '● 正在录制' : '未录制';
  $('ai-record').className = serverRecording ? 'recording' : '';
}

function updateButtons() {
  $('transcribe').toggleAttribute('disabled', !uploaded || !speechConfigured || agentBusy);
  for (const id of ['summary', 'search', 'edit']) {
    $(id).toggleAttribute('disabled', !hasTranscript || !modelConfigured || agentBusy);
  }
  $('export').toggleAttribute('disabled', !hasPlan || agentBusy);
  $('ai-play').toggleAttribute('disabled', !connected);
  $('ai-seek').toggleAttribute('disabled', !connected);
  $('record').toggleAttribute('disabled', !connected);
  $('screenshot').toggleAttribute('disabled', !connected);
  $('connect').toggleAttribute('disabled', connected || uploading || (!mediaId && !selectedFile));
  $('stop').toggleAttribute('disabled', !connected);
  $('upload').toggleAttribute('disabled', connected || agentBusy || uploading);
  $('run').toggleAttribute('disabled', !uploaded || agentBusy);
  $('ai-config').textContent = modelConfigured ? '已就绪' : '待配置';
  $('ai-config').className = `chip ${modelConfigured ? 'ready' : 'pending'}`;
  $('ai-hint').textContent = modelConfigured
    ? (speechConfigured ? '文本模型与语音模型均已就绪。' : '文本模型已就绪，语音转写不可用。')
    : '模型尚未就绪；请检查服务端 config/ 或本地模型。';
}

function renderSubtitle() {
  const node = $('subtitle');
  if (!subtitleEnabled) {
    node.textContent = '';
    return;
  }
  const current = subtitles.find((segment) => positionMs >= segment.start_ms && positionMs <= segment.end_ms);
  node.textContent = current?.text ?? '';
}

function applyPosition(position: number, duration: number, playing: boolean) {
  positionMs = position;
  durationMs = duration;
  serverPlaying = playing;
  if (!seek.matches(':active') && duration > 0) {
    seek.value = String(Math.round((position / duration) * 1000));
  }
  setPlayingUi(playing);
  updateClock();
  renderSubtitle();
  updateMediaContext();
}

async function control(action: string, position = 0): Promise<ControlResult> {
  if (!mediaId) throw new Error('尚未上传媒体');
  const response = await fetch('/control', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ session_id: sessionInput.value.trim() || WEB_SESSION, media_id: mediaId, action, position }),
  });
  const result = (await response.json()) as ControlResult;
  if (!response.ok || !result.success) throw new Error(result.error ?? `Control failed (${response.status})`);
  return result;
}

function startStatusPolling() {
  stopStatusPolling();
  statusTimer = window.setInterval(() => {
    if (!connected) return;
    void control('status').then((status) => {
      applyPosition(status.position_ms ?? 0, status.duration_ms ?? 0, status.playing ?? false);
      serverRecording = status.recording ?? false;
      $('record').textContent = serverRecording ? '停止录制' : '录制';
      updateMediaContext();
    }).catch(() => { /* transient */ });
  }, 500);
}

function stopStatusPolling() {
  if (statusTimer !== undefined) {
    window.clearInterval(statusTimer);
    statusTimer = undefined;
  }
}

async function togglePlay() {
  if (!connected) {
    await connect();
    return;
  }
  try {
    if (serverPlaying) {
      video.pause();
      await control('pause');
      applyPosition(positionMs, durationMs, false);
    } else {
      await video.play().catch(() => {});
      await control('play');
      applyPosition(positionMs, durationMs, true);
    }
  } catch (error) {
    setError(error instanceof Error ? error.message : String(error));
  }
}

async function seekTo(position: number) {
  if (!connected) return;
  try {
    const value = Math.max(0, Math.min(1, position));
    await control('seek', value);
    applyPosition(value * durationMs, durationMs, serverPlaying);
  } catch (error) {
    setError(error instanceof Error ? error.message : String(error));
  }
}

function applyFilters() {
  video.style.transform = flip ? 'scaleY(-1)' : '';
  const filters = [];
  if (gray) filters.push('grayscale(1)');
  if (invert) filters.push('invert(1)');
  video.style.filter = filters.length > 0 ? filters.join(' ') : 'none';
}

async function toggleRecording() {
  if (!connected) return;
  try {
    const result = await control(serverRecording ? 'record_stop' : 'record_start');
    serverRecording = result.recording ?? !serverRecording;
    $('record').textContent = serverRecording ? '停止录制' : '录制';
    updateMediaContext();
    if (!serverRecording && result.output_path) {
      log(`录制已保存：${result.output_path}`);
      renderResult($('ai-result'), `录制已保存：${result.output_path}`, [result.output_path]);
    } else {
      log(serverRecording ? '服务端录制已启动。' : '服务端录制已停止。');
    }
  } catch (error) {
    setError(error instanceof Error ? error.message : String(error));
  }
}

function captureScreenshot() {
  if (video.videoWidth === 0) {
    setError('当前还没有可截图的画面。');
    return;
  }
  const canvas = document.createElement('canvas');
  canvas.width = video.videoWidth;
  canvas.height = video.videoHeight;
  const context = canvas.getContext('2d');
  if (!context) return;
  const filters = getComputedStyle(video).filter;
  if (filters && filters !== 'none') context.filter = filters;
  if (flip) {
    context.translate(0, canvas.height);
    context.scale(1, -1);
  }
  context.drawImage(video, 0, 0, canvas.width, canvas.height);
  canvas.toBlob((blob) => {
    if (!blob) return;
    const link = document.createElement('a');
    link.href = URL.createObjectURL(blob);
    link.download = `avplayer-${Date.now()}.png`;
    link.click();
    URL.revokeObjectURL(link.href);
    log('截图已保存。');
  }, 'image/png');
}

async function uploadSelectedFile(): Promise<boolean> {
  if (!selectedFile) {
    setError('请选择要上传的视频。');
    return false;
  }
  if (uploading) return false;
  if (connected) await disconnect();
  uploading = true;
  $('upload').toggleAttribute('disabled', true);
  setError('');
  log(`上传 ${selectedFile.name}…`);
  try {
    const response = await fetch('/upload', {
      method: 'POST',
      headers: {
        'Content-Type': 'application/octet-stream',
        'X-Session-Id': WEB_SESSION,
        'X-File-Name': encodeURIComponent(selectedFile.name),
      },
      body: selectedFile,
    });
    const result = (await response.json()) as { media_id?: string; error?: string };
    if (!response.ok || !result.media_id) throw new Error(result.error ?? `Upload failed (${response.status})`);
    mediaId = result.media_id;
    uploaded = true;
    uploadedFile = selectedFile;
    mediaInput.value = mediaId;
    sessionInput.value = WEB_SESSION;
    subtitles = [];
    planSegments = [];
    hasTranscript = false;
    hasPlan = false;
    $('ai-result').textContent = '上传完成，可以连接播放或开始 AI 任务。';
    $('agent-output').textContent = '上传完成，可以提交 Agent 目标。';
    $('subtitle').textContent = '';
    log(`上传完成：${mediaId}`);
    updateMediaContext();
    updateButtons();
    return true;
  } catch (error) {
    setError(error instanceof Error ? error.message : String(error));
    return false;
  } finally {
    uploading = false;
    $('upload').toggleAttribute('disabled', false);
    updateButtons();
  }
}

function signalSend(kind: string, data: Record<string, string> = {}) {
  if (signalSocket?.readyState !== WebSocket.OPEN) return;
  signalSocket.send(JSON.stringify({
    session_id: sessionInput.value.trim() || WEB_SESSION,
    media_id: mediaId,
    kind,
    ...data,
  }));
}

async function connect() {
  if (!mediaId && selectedFile && !uploading) {
    // Stale media_id is impossible here, but a picked file means the user
    // expects "connect" to just work: upload first, then negotiate.
    log('尚未上传，先上传所选视频…');
    if (!(await uploadSelectedFile())) return;
  }
  if (!mediaId) {
    setError('请先选择视频并点击「上传到服务端」，再连接。');
    return;
  }
  if (connected) return;
  setError('');
  setConnection('connecting', '连接中');
  log('正在协商 WebRTC…');
  remoteStream = new MediaStream();
  peer = new RTCPeerConnection({ iceServers });
  // media_server only produces H.264 + Opus. Restrict the offer to those
  // codecs; otherwise Chrome may negotiate VP8/VP9 for the video m-line and
  // the server's H.264 track never opens.
  const videoTransceiver = peer.addTransceiver('video', { direction: 'recvonly' });
  const audioTransceiver = peer.addTransceiver('audio', { direction: 'recvonly' });
  try {
    const videoH264 = (RTCRtpReceiver.getCapabilities('video')?.codecs ?? [])
      .filter((codec) => codec.mimeType.toLowerCase() === 'video/h264');
    if (videoH264.length > 0) videoTransceiver.setCodecPreferences(videoH264);
    const audioOpus = (RTCRtpReceiver.getCapabilities('audio')?.codecs ?? [])
      .filter((codec) => codec.mimeType.toLowerCase() === 'audio/opus');
    if (audioOpus.length > 0) audioTransceiver.setCodecPreferences(audioOpus);
  } catch (error) {
    log('浏览器不支持设置编解码偏好，使用默认协商。');
  }
  peer.ontrack = (event) => {
    const stream = event.streams[0] ?? new MediaStream([event.track]);
    stream.getTracks().forEach((track) => {
      if (!remoteStream.getTracks().some((item) => item.id === track.id)) remoteStream.addTrack(track);
    });
    video.srcObject = remoteStream;
    video.muted = false;
    void video.play().catch(() => setError('浏览器阻止了自动播放，请按播放。'));
    $('empty').classList.add('hidden');
    setTimeout(updateMediaContext, 300);
    setConnection('connected', '已连接');
    startStatusPolling();
  };
  peer.onconnectionstatechange = () => {
    const state = peer?.connectionState ?? 'closed';
    if (state === 'connected') {
      connected = true;
      setConnection('connected', '已连接');
      startStatusPolling();
      updateButtons();
    } else if (state === 'failed' || state === 'disconnected') {
      setConnection('error', state.toUpperCase());
    }
  };
  peer.onicecandidate = ({ candidate }) => {
    if (candidate) signalSend('ice', { candidate: candidate.candidate, mid: candidate.sdpMid ?? '' });
  };

  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  signalSocket = new WebSocket(`${protocol}//${location.host}/signal`);
  signalSocket.onopen = async () => {
    log('信令通道已连接。');
    try {
      const offer = await peer!.createOffer();
      await peer!.setLocalDescription(offer);
      signalSend('offer', { sdp: offer.sdp ?? '' });
    } catch (error) {
      setError(error instanceof Error ? error.message : String(error));
    }
  };
  signalSocket.onmessage = async (event) => {
    const message = JSON.parse(String(event.data)) as { kind: string; sdp?: string; candidate?: string; mid?: string; error?: string };
    try {
      if (message.kind === 'answer' && message.sdp) {
        await peer!.setRemoteDescription({ type: 'answer', sdp: message.sdp });
      } else if (message.kind === 'ice' && message.candidate) {
        await peer!.addIceCandidate({ candidate: message.candidate, sdpMid: message.mid || null });
      } else if (message.kind === 'error') {
        throw new Error(message.error ?? '信令失败');
      }
    } catch (error) {
      setError(error instanceof Error ? error.message : String(error));
    }
  };
  signalSocket.onerror = () => {
    log(`信令 WebSocket 错误：${location.host}`);
    setError('无法连接 Web 网关，请确认服务已启动。');
  };
  signalSocket.onclose = (event) => {
    log(`信令已关闭（code=${event.code}）。`);
    if (connected) setConnection('error', '信令已关闭');
  };
}

async function disconnect() {
  connected = false;
  stopStatusPolling();
  signalSocket?.close();
  signalSocket = undefined;
  peer?.close();
  peer = undefined;
  video.srcObject = null;
  remoteStream = new MediaStream();
  $('empty').classList.remove('hidden');
  serverRecording = false;
  serverPlaying = false;
  $('record').textContent = '录制';
  $('subtitle').textContent = '';
  setConnection('', '未连接');
  setPlayingUi(false);
  updateButtons();
  updateMediaContext();
}

// Panel content can exceed the fixed viewport height. Scrollbars are hidden by
// design, so show a soft fade at the bottom edge while more content remains.
function updateScrollHint() {
  const page = document.querySelector<HTMLElement>('.tab-page.active');
  const sidebar = document.querySelector<HTMLElement>('.sidebar');
  if (!page || !sidebar) return;
  const more = page.scrollHeight - page.scrollTop - page.clientHeight > 4;
  sidebar.classList.toggle('has-more', more);
}

function setTab(name: string) {
  document.querySelectorAll<HTMLButtonElement>('.tab').forEach((button) => {
    button.classList.toggle('active', button.dataset.tab === name);
  });
  document.querySelectorAll<HTMLElement>('.tab-page').forEach((page) => {
    page.classList.toggle('active', page.id === `tab-${name}`);
  });
  updateScrollHint();
}

document.querySelectorAll<HTMLButtonElement>('.tab').forEach((button) => {
  button.addEventListener('click', () => setTab(button.dataset.tab ?? 'network'));
  button.addEventListener('click', () => requestAnimationFrame(updateScrollHint));
});

// ---------------------------------------------------------------------------
// AI / Agent WebSocket bridge
// ---------------------------------------------------------------------------
const AGENT_TOOLS = [
  { name: 'get_media_context', description: '读取当前上传媒体的状态。', input_schema: { type: 'object', properties: {} } },
  { name: 'export_video', description: '按时间轴导出片段：优先给 start_ms/end_ms 精确截取（毫秒），只有“随便剪一段”才用 duration_ms；可多次调用导出多个片段。', input_schema: { type: 'object', properties: { start_ms: { type: 'integer' }, end_ms: { type: 'integer' }, duration_ms: { type: 'integer' } } } },
];

function agentSend(type: string, payload: Record<string, unknown> = {}, extra: Record<string, unknown> = {}): boolean {
  if (agentSocket?.readyState !== WebSocket.OPEN) {
    setAgentStatus('Agent 服务未连接。', false);
    return false;
  }
  agentSocket.send(JSON.stringify({
    type,
    request_id: requestId(),
    session_id: WEB_SESSION,
    payload,
    ...extra,
  }));
  return true;
}

function syncProvider(value: string) {
  provider = value === 'cloud' ? 'cloud' : 'local';
  for (const select of providerSelects) {
    if (select.value !== provider) select.value = provider;
  }
  if (agentSocket?.readyState === WebSocket.OPEN) {
    agentSocket.send(JSON.stringify({
      type: 'provider',
      request_id: requestId(),
      session_id: WEB_SESSION,
      payload: { provider },
    }));
  }
  $('ai-hint').textContent = provider === 'cloud'
    ? '豆包 API：文本任务发送字幕，转写时上传音频。'
    : '本地模型：音频和字幕留在服务端。';
}

function setAgentStatus(text: string, ready: boolean) {
  $('agent-status').textContent = text;
  $('agent-runtime').textContent = ready ? '运行中' : '待接入';
  $('agent-runtime').className = `chip ${ready ? 'ready' : 'pending'}`;
}

function appendPlan(text: string) {
  const list = $('agent-plan');
  const empty = list.querySelector('.muted');
  if (empty) empty.remove();
  const item = document.createElement('li');
  item.textContent = text;
  list.appendChild(item);
}

// One card per tool call, updated in place. Rendering a card per status
// message left "执行中…" duplicates stacked above the completed result.
function upsertTool(tool: string, callId: string, state: 'running' | 'ok' | 'fail', detail: string) {
  const container = $('agent-tools');
  container.querySelector('.muted')?.remove();
  let row = callId
    ? container.querySelector<HTMLElement>(`.tool-row[data-call-id="${callId}"]`)
    : null;
  if (!row) {
    row = document.createElement('div');
    row.dataset.callId = callId;
    row.innerHTML = '<strong></strong><span></span>';
    container.prepend(row);
  }
  row.className = `tool-row ${state}`;
  const name = row.querySelector('strong');
  const note = row.querySelector('span');
  if (name) name.textContent = tool;
  if (note) note.textContent = detail;
}

function settlePendingTools(reason = '未返回结果') {
  document.querySelectorAll<HTMLElement>('.tool-row.running').forEach((row) => {
    row.className = 'tool-row fail';
    const note = row.querySelector('span');
    if (note) note.textContent = reason;
  });
}

// A task that never reports back must not leave the panel stuck on
// "处理中" forever; release the UI after a generous timeout.
function armAgentWatchdog() {
  window.clearTimeout(agentWatchdog);
  agentWatchdog = window.setTimeout(() => {
    agentWatchdog = undefined;
    if (!agentBusy) return;
    agentBusy = false;
    const message = '任务超时未返回，已解除等待状态；请重试或检查服务端日志。';
    $('ai-status').textContent = message;
    $('agent-status').textContent = message;
    setError(message);
    updateButtons();
  }, 10 * 60 * 1000);
}

function clearAgentWatchdog() {
  window.clearTimeout(agentWatchdog);
  agentWatchdog = undefined;
}

function connectAgent() {
  if (agentSocket && (agentSocket.readyState === WebSocket.OPEN || agentSocket.readyState === WebSocket.CONNECTING)) return;
  const protocol = location.protocol === 'https:' ? 'wss:' : 'ws:';
  agentSocket = new WebSocket(`${protocol}//${location.host}/agent`);
  agentSocket.onopen = () => {
    clearAgentWatchdog();
    agentBusy = false;
    setAgentStatus('Agent 已连接。', true);
    agentSend('tool_catalog', { tools: AGENT_TOOLS });
    syncProvider(provider);
    updateButtons();
  };
  agentSocket.onmessage = (event) => handleAgentMessage(JSON.parse(String(event.data)) as AgentMessage);
  agentSocket.onerror = () => setAgentStatus('Agent 服务不可达。', false);
  agentSocket.onclose = () => {
    setAgentStatus('Agent 服务已断开。', false);
    updateButtons();
    if (!intentionalAgentClose && agentRetry === undefined) {
      agentRetry = window.setTimeout(() => {
        agentRetry = undefined;
        connectAgent();
      }, 3000);
    }
  };
}

function handleTaskResult(message: AgentMessage) {
  clearAgentWatchdog();
  agentBusy = false;
  updateButtons();
  const payload = message.payload ?? {};
  const task = String(payload.task ?? '');
  const success = Boolean(payload.success);
  if (!success) {
    const error = String(payload.error ?? '任务失败');
    $('ai-result').textContent = error;
    $('ai-status').textContent = error;
    return;
  }
  const result = String(payload.result ?? '');
  const paths = Array.isArray(payload.paths) ? payload.paths as string[] : artifactPaths(result);
  renderResult($('ai-result'), result || '任务完成。', paths);
  $('ai-status').textContent = '任务完成。';

  const segments = Array.isArray(payload.segments) ? payload.segments as Record<string, unknown>[] : [];
  if (task === 'transcribe_subtitle' && segments.length > 0) {
    subtitles = segments.map((item) => ({
      start_ms: Number(item.start_ms ?? 0),
      end_ms: Number(item.end_ms ?? 0),
      text: String(item.text ?? ''),
    })).filter((item) => item.end_ms > item.start_ms && item.text.length > 0);
    hasTranscript = subtitles.length > 0;
    hasPlan = false;
    planSegments = [];
    renderSubtitle();
    $('ai-status').textContent = `已生成 ${subtitles.length} 条字幕。`;
  }
  if (task === 'smart_edit') {
    planSegments = segments.map((item) => ({
      start_ms: Number(item.start_ms ?? 0),
      end_ms: Number(item.end_ms ?? 0),
      reason: String(item.reason ?? ''),
    })).filter((item) => item.end_ms > item.start_ms);
    hasPlan = planSegments.length > 0;
    $('ai-status').textContent = `已生成 ${planSegments.length} 段剪辑建议。`;
  }
  if (task === 'export_plan') {
    const exported = Array.isArray(payload.paths) ? payload.paths as string[] : [];
    renderResult($('ai-result'), exported.length > 0
      ? `已导出 ${exported.length} 段：\n${exported.join('\n')}`
      : result, exported);
  }
  updateButtons();
}

function handleAgentMessage(message: AgentMessage) {
  const payload = message.payload ?? {};
  if (message.type === 'provider_status' || message.type === 'ready') {
    modelConfigured = Boolean(payload.model_configured);
    speechConfigured = Boolean(payload.speech_configured);
    const configuredProvider = String(payload.provider ?? provider);
    if (configuredProvider !== provider) syncProvider(configuredProvider);
    $('ai-status').textContent = String(payload.config_status ?? '');
    updateButtons();
    return;
  }
  if (message.type === 'task_result') {
    handleTaskResult(message);
    return;
  }
  if (message.type === 'status') {
    const stage = String(payload.stage ?? '');
    const tool = message.tool || String(payload.tool ?? '');
    const detail = String(payload.status ?? '');
    if (stage === 'planning') {
      const items = $('agent-plan').querySelectorAll('li');
      const last = items.length > 0 ? items[items.length - 1].textContent : '';
      if (last !== '规划目标') appendPlan('规划目标');
      $('agent-status').textContent = detail;
    } else if (stage === 'tool_started') {
      appendPlan(`执行 ${tool}`);
      upsertTool(tool, String(payload.call_id ?? ''), 'running', '执行中…');
      $('agent-status').textContent = detail;
    } else if (stage === 'tool_completed') {
      upsertTool(tool, String(payload.call_id ?? ''), payload.success ? 'ok' : 'fail', detail);
      $('agent-status').textContent = detail;
    } else if (detail) {
      $('agent-status').textContent = detail;
    }
    return;
  }
  if (message.type === 'final') {
    clearAgentWatchdog();
    agentBusy = false;
    settlePendingTools('任务结束前未返回结果');
    const finalText = String(payload.text ?? '') || '模型没有返回文字结论，请查看工具执行结果或重试。';
    renderResult($('agent-output'), finalText, artifactPaths(finalText));
    $('agent-status').textContent = '任务已结束。';
    updateButtons();
    return;
  }
  if (message.type === 'error') {
    clearAgentWatchdog();
    agentBusy = false;
    settlePendingTools('任务已中断');
    const error = String(payload.error ?? message.error ?? 'Agent 错误');
    $('agent-output').textContent = error;
    $('agent-status').textContent = error;
    updateButtons();
    return;
  }
  if (message.type === 'tool_call') {
    const tool = message.tool ?? '';
    let result: Record<string, unknown> = { success: false, error: `Web 端暂不支持工具：${tool}` };
    if (tool === 'player_control') {
      const action = String(message.arguments?.action ?? '');
      if (action === 'play_pause') {
        void togglePlay();
        result = { success: true };
      } else if (action === 'seek_start') {
        void seekTo(0);
        result = { success: true };
      }
    } else if (tool === 'capture_screenshot') {
      captureScreenshot();
      result = { success: true, path: 'browser-download' };
    }
    if (agentSocket?.readyState === WebSocket.OPEN) {
      agentSocket.send(JSON.stringify({
        type: 'tool_result',
        request_id: message.request_id,
        session_id: message.session_id ?? WEB_SESSION,
        tool,
        payload: result,
      }));
    }
  }
}

function runAiTask(task: string, args: Record<string, unknown> = {}) {
  if (!uploaded) {
    setError('请先上传媒体。');
    return;
  }
  if (agentBusy) return;
  agentBusy = true;
  armAgentWatchdog();
  updateButtons();
  $('ai-result').textContent = '处理中…';
  $('ai-status').textContent = '任务处理中…';
  if (!agentSend('task', { task }, { arguments: args })) {
    agentBusy = false;
    updateButtons();
  }
}

function runGoal(goal: string) {
  const value = goal.trim();
  if (!value) {
    setError('请输入任务目标。');
    return;
  }
  if (!uploaded) {
    setError('请先上传媒体。');
    return;
  }
  agentBusy = true;
  armAgentWatchdog();
  updateButtons();
  $('agent-plan').innerHTML = '<li class="muted">正在规划…</li>';
  $('agent-tools').innerHTML = '<span class="muted">工具尚未调用</span>';
  $('agent-output').textContent = '任务执行中…';
  setAgentStatus('正在提交目标…', true);
  if (!agentSend('goal', { goal: value })) {
    agentBusy = false;
    updateButtons();
  }
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------
$('import').addEventListener('click', () => fileInput.click());
fileInput.addEventListener('change', () => {
  const picked = fileInput.files?.[0] ?? null;
  selectedFile = picked;
  $('file-name').textContent = picked ? picked.name : '未选择文件';
  if (picked && picked !== uploadedFile) {
    // A new file invalidates the previous media_id and its AI results.
    uploaded = false;
    mediaId = '';
    mediaInput.value = '';
    subtitles = [];
    planSegments = [];
    hasTranscript = false;
    hasPlan = false;
    $('subtitle').textContent = '';
    setError('');
    updateMediaContext();
    updateButtons();
    log(`已选择：${picked.name}（待上传）`);
  } else if (picked) {
    log(`已选择：${picked.name}`);
  }
});
$('upload').addEventListener('click', () => {
  if (!selectedFile) {
    fileInput.click();
  } else {
    void uploadSelectedFile();
  }
});
mediaInput.addEventListener('change', () => {
  const value = mediaInput.value.trim();
  if (value && value !== mediaId) {
    mediaId = value;
    uploaded = true;
    updateMediaContext();
    updateButtons();
  }
});
$('connect').addEventListener('click', () => {
  mediaId = mediaInput.value.trim() || mediaId;
  uploaded = uploaded || Boolean(mediaId);
  void connect();
});
$('stop').addEventListener('click', () => void disconnect());
$('play').addEventListener('click', () => void togglePlay());
$('ai-play').addEventListener('click', () => void togglePlay());
$('ai-seek').addEventListener('click', () => void seekTo(0));
$('record').addEventListener('click', () => void toggleRecording());
$('screenshot').addEventListener('click', captureScreenshot);
$('flip').addEventListener('click', () => {
  flip = !flip;
  $('flip').classList.toggle('active', flip);
  applyFilters();
});
$('gray').addEventListener('click', () => {
  gray = !gray;
  $('gray').classList.toggle('active', gray);
  applyFilters();
});
$('invert').addEventListener('click', () => {
  invert = !invert;
  $('invert').classList.toggle('active', invert);
  applyFilters();
});
seek.addEventListener('change', () => void seekTo(Number(seek.value) / 1000));
seek.addEventListener('input', () => {
  if (durationMs > 0) {
    $('elapsed').textContent = formatTime((Number(seek.value) / 1000) * durationMs);
  }
});
$('subtitle-toggle').addEventListener('change', () => {
  subtitleEnabled = $<HTMLInputElement>('subtitle-toggle').checked;
  renderSubtitle();
});
for (const select of providerSelects) {
  select.addEventListener('change', () => syncProvider(select.value));
}
$('transcribe').addEventListener('click', () => runAiTask('transcribe_subtitle'));
$('summary').addEventListener('click', () => runAiTask('summarize_video'));
$('search').addEventListener('click', () => runAiTask('semantic_search', { query: $<HTMLInputElement>('prompt').value }));
$('edit').addEventListener('click', () => runAiTask('smart_edit', { instruction: $<HTMLInputElement>('prompt').value }));
$('export').addEventListener('click', () => runAiTask('export_plan', { segments: planSegments }));
$('run').addEventListener('click', () => runGoal($<HTMLInputElement>('goal').value));
$('goal').addEventListener('keydown', (event) => {
  if (event.key === 'Enter') runGoal($<HTMLInputElement>('goal').value);
});
$('quick-summary').addEventListener('click', () => runGoal('整理当前视频内容并给出摘要'));
$('quick-search').addEventListener('click', () => runGoal('查找当前视频中的关键片段'));
$('agent-stop').addEventListener('click', () => {
  agentBusy = false;
  intentionalAgentClose = true;
  agentSocket?.close();
  agentSocket = undefined;
  setAgentStatus('任务已停止，可重新提交目标。', false);
  updateButtons();
  window.setTimeout(() => {
    intentionalAgentClose = false;
    connectAgent();
  }, 600);
});

video.addEventListener('loadedmetadata', updateMediaContext);
video.addEventListener('click', () => void togglePlay());
video.addEventListener('play', () => {
  setPlayingUi(true);
  if ($('error').textContent === '浏览器阻止了自动播放，请按播放。') setError('');
});
video.addEventListener('pause', () => setPlayingUi(false));

// ---------------------------------------------------------------------------
// Boot
// ---------------------------------------------------------------------------
updateButtons();
updateMediaContext();
setConnection('', '未连接');
document.querySelectorAll<HTMLElement>('.tab-page').forEach((page) => {
  page.addEventListener('scroll', updateScrollHint, { passive: true });
});
window.addEventListener('resize', updateScrollHint);
new ResizeObserver(updateScrollHint).observe(document.querySelector<HTMLElement>('.sidebar')!);
const initialTab = new URLSearchParams(location.search).get('tab');
if (initialTab) setTab(initialTab);
connectAgent();
void fetch('/config')
  .then((response) => response.ok ? response.json() : Promise.reject(new Error(String(response.status))))
  .then((config: { ice_servers?: RTCIceServer[]; provider?: string }) => {
    if (Array.isArray(config.ice_servers) && config.ice_servers.length > 0) iceServers = config.ice_servers;
    syncProvider(config.provider ?? 'local');
  })
  .catch(() => log('无法读取服务端配置，使用默认 ICE。'));

if (location.protocol === 'http:' && location.hostname !== 'localhost' && location.hostname !== '127.0.0.1') {
  log('当前不是安全上下文，浏览器可能限制 WebRTC。');
}

// Vite HMR swaps this module without reloading the page. The previous
// WebSocket would stay open and keep competing for the same session stream,
// so results could be delivered to a socket whose handlers no longer update
// the UI. Close both sockets when this module is disposed.
if (import.meta.hot) {
  import.meta.hot.dispose(() => {
    intentionalAgentClose = true;
    agentSocket?.close();
    signalSocket?.close();
  });
}
