// 仅本轮离线验收使用：两个固定隔离端口、自有子进程、显式全新数据目录。
import http from 'node:http';
import net from 'node:net';
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
const root = process.env.WVD_CLOSURE_ROOT;
if (!root || !path.isAbsolute(root)) throw new Error('WVD_CLOSURE_ROOT_REQUIRED');
const candidate = path.join(root, 'candidate');
for (const port of [18754, 18755]) await new Promise((resolve, reject) => {
  const probe = net.createServer(); probe.once('error', reject);
  probe.listen(port, '127.0.0.1', () => probe.close(resolve));
});
const data = fs.mkdtempSync(path.join(root, 'api-data-'));
let child, stopping = false;
const history = [];
async function startNative() {
  child = spawn(path.join(candidate, 'automationd.exe'), ['--no-browser', '--port', '18755',
    '--web-root', path.join(candidate, 'web'), '--data-root', data,
    '--pack-root', path.join(candidate, 'pack'), '--quests', path.join(candidate, 'data/quest.json')],
    { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  history.push({ pid: child.pid, executable: path.join(candidate, 'automationd.exe'), data, port: 18755 });
  fs.writeFileSync(path.join(root, 'owned-test-processes.json'), JSON.stringify({ server: process.pid, children: history }, null, 2));
  const log = fs.createWriteStream(path.join(root, 'logs/native-api.log'), { flags: 'a' });
  child.stdout.pipe(log, { end: false }); child.stderr.pipe(log, { end: false });
  child.once('exit', () => log.end());
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('CLOSURE_NATIVE_START_TIMEOUT')), 45000);
    child.once('error', error => { clearTimeout(timer); reject(error); });
    child.once('exit', code => { clearTimeout(timer); reject(new Error(`CLOSURE_NATIVE_EXIT:${code}`)); });
    child.stdout.on('data', buffer => { if (buffer.toString().includes('READY ')) { clearTimeout(timer); resolve(); } });
  });
}
async function stopNative() {
  const owned = child;
  if (!owned || owned.exitCode !== null) return;
  await new Promise(resolve => { owned.once('exit', resolve); owned.kill(); });
}
await startNative();
const server = http.createServer(async (request, response) => {
  if (request.url === '/__closure/restart-native' && request.method === 'POST') {
    try { await stopNative(); await startNative(); response.writeHead(200); response.end('{}'); }
    catch (error) { response.writeHead(500); response.end(String(error)); }
    return;
  }
  if (request.method !== 'GET' || request.url.startsWith('/api/')) { response.writeHead(403); response.end('CLOSURE_UNREGISTERED_REQUEST'); return; }
  const relative = decodeURIComponent(new URL(request.url, 'http://127.0.0.1:18754').pathname);
  const file = path.resolve(candidate, 'web', relative === '/' ? 'index.html' : relative.slice(1));
  if (!file.startsWith(path.join(candidate, 'web') + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) {
    response.writeHead(404); response.end(); return;
  }
  const type = { '.js': 'text/javascript', '.css': 'text/css', '.html': 'text/html', '.png': 'image/png', '.svg': 'image/svg+xml' }[path.extname(file)] ?? 'application/octet-stream';
  response.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' }); fs.createReadStream(file).pipe(response);
});
server.listen(18754, '127.0.0.1');
async function cleanup() {
  if (stopping) return; stopping = true; server.close(); await stopNative();
  fs.writeFileSync(path.join(root, 'test-server-cleanup.json'), JSON.stringify({ ownedOnly: true, history, stopped: true }));
  process.exit(0);
}
process.on('SIGINT', cleanup); process.on('SIGTERM', cleanup);
