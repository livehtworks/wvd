import { test, expect, type Page, type Route } from '@playwright/test';

const copy = <T>(value: T): T => JSON.parse(JSON.stringify(value));
const profile = () => ({ revision: 'profile-1', effective_source: 'default', profile: {
  FARM_TARGET: 'Scorpionesses', FARM_TARGET_TEXT: '蝎女', TASK_SPECIFIC_CONFIG: false,
  EMU_PATH: 'isolated-not-a-device', EMU_INDEX: 2, ADB_ADRESS: 'offline', AUTO_START_CLASH: false,
  WHO_WILL_OPEN_IT: 0, REST_INTERVEL: 1, MAX_TRY_LIMIT: 25, MAX_CRASH_LIMIT: 10,
  DEFAULT_OVERALL_STRATEGY: '方案A', RELOAD_STRATEGY_WHEN: '每场战斗前', LANGUAGE: 'zh_CN',
  UNKNOWN_KEEP: { nested: ['unchanged'] },
  TASK_POINT_STRATEGY: { overall_strategy: '', task_point: {}, special_combat: {
    skull: false, portrait: false, portrait_image: 'retained-custom', normal_strategy: '方案A', special_strategy: '方案A' } },
  STRATEGY: [{ group_name: '方案A', skill_settings: [{ role_var: '未知角色', skill_var: '左上技能', skill_lvl: 7, target_var: 'next', freq_var: 'repeat' }] }],
} });
const flow = () => ({ id: 'closure-flow', revision: 'flow-1', name: '离线流程', description: '保留说明',
  entry_node_id: 'done', nodes: [{ id: 'done', type: 'editor', position: { x: 50, y: 50 }, data: { label: '结束', node_type: 'end', parameters: { outcome: 'success' } } }],
  edges: [], layout: { viewport: { x: 3, y: 5, zoom: 0.8 } }, events: {}, checks: { phase: 'business', inherit: [] },
  interface: { parameters: [], outputs: [], handoffs: [] }, unknown: { retained: true } });
type Recorded = { url: string; method: string; request_id?: string; body: any };
async function fixture(page: Page) {
  const writes: Recorded[] = [], violations: string[] = [], requests: string[] = [];
  const state = { profile: profile(), flow: flow(), run: { state: 'Idle', quiescent: true, busy: false } as any,
    failRun: false, failDevice: false, runFlight: 0, peakRun: 0, deviceFlight: 0, peakDevice: 0,
    screenshot: false, holdRun: undefined as undefined | (() => Promise<void>),
    handlers: new Map<string, (route: Route, body: any) => Promise<void>>() };
  await page.route('**/*', async route => {
    const request = route.request(), url = new URL(request.url()), key = `${request.method()} ${url.pathname}`;
    requests.push(key);
    if (url.origin !== 'http://127.0.0.1:18754') { violations.push(key); await route.abort(); return; }
    if (!url.pathname.startsWith('/api/')) { await route.continue(); return; }
    const body = request.postDataJSON();
    if (request.method() !== 'GET') writes.push({ url: url.pathname, method: request.method(), request_id: body?.request_id, body });
    if (state.handlers.has(key)) { await state.handlers.get(key)!(route, body); return; }
    if (key === 'GET /api/v1/version') return route.fulfill({ json: { version: 'closure-candidate', api_version: 1 } });
    if (key === 'GET /api/v1/profile') return route.fulfill({ json: copy(state.profile) });
    if (key === 'GET /api/v1/catalog') return route.fulfill({ json: { tasks: [
      { id: 'Scorpionesses', name: '蝎女', category: '副本', type: 'dungeon' }, { id: 'B', name: '任务B', category: '副本', type: 'dungeon' },
      { id: 'C', name: '任务C', category: '副本' }, { id: 'Temple', name: '炉壶灵庙', category: '日常' }],
      task_categories: [{ value: '副本', label: '副本' }], chest_openers: [{ value: 0, label: '随机' }],
      node_types: [{ type: 'end', label: '结束', defaults: { outcome: 'success' } }], skill_levels: [{ value: 7, label: '7' }] } });
    if (key === 'GET /api/v1/workflows') return route.fulfill({ json: { workflows: [state.flow] } });
    if (key === 'GET /api/v1/workflows/closure-flow') return route.fulfill({ json: state.flow });
    if (key === 'GET /api/v1/runs/current') {
      state.runFlight++; state.peakRun = Math.max(state.peakRun, state.runFlight);
      const frozen = copy(state.run), fail = state.failRun;
      try { if (state.holdRun) await state.holdRun(); if (fail) await route.abort('failed'); else await route.fulfill({ json: frozen }); }
      finally { state.runFlight--; }
      return;
    }
    if (key === 'GET /api/v1/device') {
      state.deviceFlight++; state.peakDevice = Math.max(state.peakDevice, state.deviceFlight);
      try { if (state.failDevice) await route.abort('failed'); else await route.fulfill({ json: { connected: false, captured_at: '2026-09-27T00:00:00Z',
        ...(state.screenshot ? { screenshot_url: '/api/v1/device/frame' } : {}) } }); }
      finally { state.deviceFlight--; }
      return;
    }
    if (key === 'GET /api/v1/device/frame' && state.screenshot) return route.fulfill({ contentType: 'image/png',
      body: Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVQIHWP4z8DwHwAFgAI/ScLbtAAAAABJRU5ErkJggg==', 'base64') });
    violations.push(key); await route.abort('blockedbyclient');
  });
  return { state, writes, violations, requests };
}
const deferred = () => { let resolve!: () => void; const promise = new Promise<void>(done => { resolve = done; }); return { promise, resolve }; };
async function home(page: Page) { await page.goto('/'); await expect(page.getByRole('button', { name: '开始任务', exact: true })).toBeEnabled(); }

test('UI-布局：五视口、三页签、首屏动作、停止与截图焦点', async ({ page }, info) => {
  const f = await fixture(page); f.state.screenshot = true;
  for (const [width, height] of [[1440,900],[1366,768],[1920,1080],[800,900],[390,844]]) {
    await page.setViewportSize({ width, height }); await home(page);
    for (const name of ['停止', '保存配置', '开始任务']) {
      const box = await page.getByRole('button', { name, exact: true }).boundingBox();
      expect(box, name).not.toBeNull(); expect(box!.y).toBeGreaterThanOrEqual(0); expect(box!.y + box!.height).toBeLessThanOrEqual(height);
    }
    for (const name of ['任务目标', '游戏素材语言', '循环模式']) {
      const box = await page.getByRole('combobox', { name, exact: true }).boundingBox();
      expect(box!.y).toBeGreaterThanOrEqual(0); expect(box!.y + box!.height).toBeLessThanOrEqual(height);
    }
    for (const tab of ['常用参数', '战斗方案', '设备与高级']) {
      await page.getByRole('button', { name: tab, exact: true }).click();
      expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(true);
      await page.screenshot({ path: info.outputPath(`${width}-${height}-${tab}.png`) });
    }
    const number = page.getByLabel('实例编号'); const widthNumber = (await number.boundingBox())!.width;
    expect(widthNumber).toBeGreaterThanOrEqual(88); expect(widthNumber).toBeLessThanOrEqual(128);
    await page.getByRole('button', { name: '查看已有截图' }).click();
    await expect(page.locator('dialog[open]')).toBeVisible();
    const stop = page.getByRole('button', { name: '停止', exact: true }); await stop.focus(); await expect(stop).toBeFocused();
    await page.locator('dialog button').focus(); await page.keyboard.press('Escape');
    await expect(page.getByRole('button', { name: '查看已有截图' })).toBeFocused();
    await page.evaluate(() => window.scrollTo(0, document.body.scrollHeight));
    const box = await stop.boundingBox(); expect(box!.y).toBeGreaterThanOrEqual(0); expect(box!.y + box!.height).toBeLessThanOrEqual(height);
    await page.getByRole('button', { name: '流程编辑', exact: true }).click();
    await expect(page.getByLabel('流程名称')).toBeVisible();
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(true);
    await page.screenshot({ path: info.outputPath(`${width}-${height}-workflow.png`) });
  }
  expect(f.violations).toEqual([]); expect(f.writes).toEqual([]);
  expect(f.requests.filter(value => /migration|reference-assets/.test(value))).toEqual([]);
});

test('UI-工作台写入：不可变快照、双击锁、失败保留、字典规范化', async ({ page }, info) => {
  const f = await fixture(page);
  (f.state.profile.profile as any).STRATEGY = { '方案A': { skill_settings: [], unknown: 42 } };
  await home(page); await expect(page.getByRole('button', { name: '保存配置' })).toBeDisabled();
  await page.getByRole('button', { name: '战斗方案', exact: true }).click();
  await page.getByLabel('方案名称').fill('改名方案');
  const lock = deferred();
  f.state.handlers.set('PUT /api/v1/profile', async route => { await lock.promise; await route.fulfill({ status: 409, json: { error_code: 'PROFILE_REVISION_CONFLICT', message: 'fixture' } }); });
  await page.getByRole('button', { name: '保存配置' }).dblclick({ delay: 30 });
  await expect(page.getByLabel('方案名称')).toBeDisabled();
  await expect(page.getByRole('button', { name: '流程编辑', exact: true })).toBeDisabled();
  await expect(page.getByRole('button', { name: '停止', exact: true })).toBeEnabled();
  expect(f.writes).toHaveLength(1); const sent = copy(f.writes[0].body);
  f.state.handlers.set('POST /api/v1/runs/current/stop', async route => { await route.fulfill({ json: { accepted: true } }); });
  await page.getByRole('button', { name: '停止', exact: true }).click();
  await expect.poll(() => f.writes.filter(w => w.url.endsWith('/stop')).length).toBe(1);
  lock.resolve(); await expect(page.getByRole('alert')).toContainText('PROFILE_REVISION_CONFLICT');
  await expect(page.getByLabel('方案名称')).toHaveValue('改名方案');
  await expect(page.getByRole('button', { name: '保存配置' })).toBeEnabled();
  expect((await page.getByText('有未保存更改', { exact: true }).boundingBox())!.y).toBeLessThan(900);
  expect(sent.strategy_renames).toEqual({ '方案A': '改名方案' }); expect(sent.profile.UNKNOWN_KEEP).toEqual({ nested: ['unchanged'] });
  expect(sent.profile.TASK_POINT_STRATEGY.special_combat.portrait_image).toBe('retained-custom');
  f.state.handlers.set('PUT /api/v1/profile', async (route, body) => { f.state.profile = { ...body, revision: 'profile-2' }; await route.fulfill({ json: f.state.profile }); });
  await page.getByRole('button', { name: '保存配置' }).click();
  await expect(page.getByRole('button', { name: '保存配置' })).toBeDisabled();
  const saves = f.writes.filter(w => w.method === 'PUT'); expect(saves).toHaveLength(2); expect(saves[1].body).toEqual(sent);
  await page.reload(); await page.getByRole('button', { name: '战斗方案', exact: true }).click();
  await expect(page.getByLabel('方案名称')).toHaveValue('改名方案');
  expect(f.violations).toEqual([]); await info.attach('writes', { body: JSON.stringify(f.writes), contentType: 'application/json' });
});

test('UI-任务选择：乱序、失败恢复、真正保存基线、灵庙同链', async ({ page }) => {
  const f = await fixture(page), b = deferred(), c = deferred();
  let fail = false;
  f.state.handlers.set('POST /api/v1/profile/effective', async (route, body) => {
    if (body.task_id === 'B') await b.promise;
    if (body.task_id === 'C') await c.promise;
    if (fail) { await route.fulfill({ status: 500, json: { error_code: 'LOAD_FAILED', message: 'fixture' } }); return; }
    const value = copy(f.state.profile); value.profile.FARM_TARGET = body.task_id; value.profile.FARM_TARGET_TEXT = body.task_id;
    value.profile.REST_INTERVEL = body.task_id === 'B' ? 8 : 9; await route.fulfill({ json: value });
  });
  await home(page); await page.getByRole('combobox', { name: '任务目标', exact: true }).selectOption('B');
  await page.getByRole('combobox', { name: '任务目标', exact: true }).selectOption('C');
  await expect(page.getByRole('button', { name: '保存配置' })).toBeDisabled();
  await expect(page.getByRole('button', { name: '开始任务' })).toBeDisabled();
  c.resolve(); await expect(page.getByLabel('旅店间隔')).toHaveValue('9'); b.resolve();
  await expect(page.getByRole('combobox', { name: '任务目标', exact: true })).toHaveValue('C');
  await expect(page.getByRole('button', { name: '保存配置' })).toBeEnabled();
  await page.getByRole('button', { name: '重载', exact: true }).click();
  await expect(page.getByRole('combobox', { name: '任务目标', exact: true })).toHaveValue('Scorpionesses');
  await expect(page.getByLabel('旅店间隔')).toHaveValue('1');
  fail = true; await page.getByRole('combobox', { name: '任务目标', exact: true }).selectOption('B');
  await expect(page.getByRole('alert')).toContainText('LOAD_FAILED');
  await expect(page.getByRole('combobox', { name: '任务目标', exact: true })).toHaveValue('Scorpionesses');
  fail = false; await page.getByRole('button', { name: '设备与高级', exact: true }).click();
  await page.getByRole('button', { name: '灵庙已刷新，切换目标' }).click();
  await expect(page.getByRole('combobox', { name: '任务目标', exact: true })).toHaveValue('Temple');
  await expect(page.getByLabel('灵庙切换记录')).not.toHaveValue('');
  await expect(page.getByRole('button', { name: '保存配置' })).toBeEnabled(); expect(f.violations).toEqual([]);
  f.state.handlers.set('PUT /api/v1/profile', async (route, body) => { f.state.profile = body; await route.fulfill({ json: body }); });
  await page.getByRole('button', { name: '保存配置' }).click(); await expect(page.getByRole('button', { name: '保存配置' })).toBeDisabled();
  await page.reload(); await expect(page.getByRole('combobox', { name: '任务目标', exact: true })).toHaveValue('Temple');
});

test('UI-作者写入：所有编辑面与导航锁定、失败后草稿保留', async ({ page }) => {
  const f = await fixture(page), lock = deferred();
  f.state.handlers.set('PUT /api/v1/workflows/closure-flow', async route => { await lock.promise; await route.fulfill({ status: 409, json: { error_code: 'WORKFLOW_REVISION_CONFLICT', message: 'fixture' } }); });
  await home(page); await page.getByRole('button', { name: '流程编辑', exact: true }).click();
  await page.getByLabel('流程说明').fill('modified');
  await page.getByRole('button', { name: '保存', exact: true }).dblclick({ delay: 30 });
  await expect(page.getByLabel('流程说明')).toBeDisabled();
  await expect(page.getByRole('combobox', { name: '流程', exact: true })).toBeDisabled();
  await expect(page.getByRole('button', { name: '新建流程' })).toBeDisabled();
  await expect(page.getByRole('button', { name: '撤销', exact: true })).toBeDisabled();
  await expect(page.getByRole('button', { name: '工作台', exact: true })).toBeDisabled();
  await expect(page.getByRole('button', { name: '停止', exact: true })).toBeEnabled();
  const lockedNode = page.locator('.vue-flow__node[data-id="done"]');
  const bounds = await lockedNode.boundingBox(); expect(bounds).toBeTruthy();
  await page.mouse.move(bounds!.x + 20, bounds!.y + 20); await page.mouse.down();
  await page.mouse.move(bounds!.x + 100, bounds!.y + 80, { steps: 5 }); await page.mouse.up();
  await page.keyboard.press('Delete'); await page.keyboard.press('Control+z'); await page.keyboard.press('Control+Shift+z');
  // 直接触发按钮处理器也不能绕过store锁；不只依赖disabled的浏览器行为。
  await page.getByRole('button', { name: '新建流程' }).dispatchEvent('click');
  await page.getByRole('button', { name: '撤销', exact: true }).dispatchEvent('click');
  expect(f.writes).toHaveLength(1); expect(f.writes[0].body.unknown).toEqual({ retained: true });
  expect(f.writes[0].body.layout).toEqual(f.state.flow.layout);
  lock.resolve(); await expect(page.getByRole('alert')).toContainText('WORKFLOW_REVISION_CONFLICT');
  await expect(page.getByLabel('流程说明')).toHaveValue('modified'); expect(f.violations).toEqual([]);
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect(page.getByLabel('流程说明')).toBeEnabled();
  expect(f.writes).toHaveLength(2); expect(f.writes[1].body).toEqual(f.writes[0].body);
});

test('UI-会话：失联禁启动、失败清理仍忙、旧GET不能覆盖停止、跨页请求身份', async ({ page }, info) => {
  const f = await fixture(page);
  await home(page);
  f.state.failDevice = true; await page.getByRole('button', { name: '刷新运行状态' }).click();
  await expect(page.getByRole('button', { name: '开始任务' })).toBeEnabled();
  f.state.failRun = true; await page.getByRole('button', { name: '刷新运行状态' }).click();
  await expect(page.getByLabel('当前运行会话')).toContainText('状态未知');
  await expect(page.getByRole('button', { name: '开始任务' })).toBeDisabled();
  await expect(page.getByRole('button', { name: '停止', exact: true })).toBeEnabled();
  f.state.failRun = false; f.state.run = { state: 'Failed', run_id: 5, quiescent: false };
  await page.getByRole('button', { name: '刷新运行状态' }).click();
  await expect(page.getByLabel('当前运行会话')).toContainText('仍在清理');
  await expect(page.getByRole('button', { name: '开始任务' })).toBeDisabled();
  f.state.run = { state: 'Idle', quiescent: true }; await page.getByRole('button', { name: '刷新运行状态' }).click();
  await expect(page.getByRole('button', { name: '开始任务' })).toBeEnabled();
  f.state.handlers.set('POST /api/v1/runs/start', async route => { await route.abort('failed'); });
  await page.getByRole('button', { name: '开始任务' }).click();
  await expect(page.getByRole('button', { name: '开始任务' })).toBeEnabled();
  const id = f.writes.at(-1)!.request_id; expect(id).toBeTruthy();
  await page.getByRole('button', { name: '流程编辑', exact: true }).click();
  await page.getByRole('button', { name: '工作台', exact: true }).click();
  await page.getByRole('button', { name: '开始任务' }).click();
  await expect.poll(() => f.writes.filter(w => w.url.endsWith('/start')).length).toBe(2);
  expect(f.writes.at(-1)!.request_id).toBe(id);
  f.state.handlers.set('POST /api/v1/runs/current/stop', async (route, body) => {
    expect(body.request_id).toBe(id); f.state.run = { state: 'StopRequested', quiescent: false, run_id: 7 };
    await route.fulfill({ json: { accepted: true } });
  });
  await expect(page.getByRole('button', { name: '开始任务' })).toBeEnabled();
  const hold = deferred(); f.state.holdRun = () => hold.promise;
  await page.getByRole('button', { name: '刷新运行状态' }).click();
  await expect.poll(() => f.state.runFlight).toBe(1);
  await page.getByRole('button', { name: '停止', exact: true }).click();
  f.state.holdRun = undefined; hold.resolve();
  await expect(page.getByLabel('当前运行会话')).toContainText('StopRequested');
  expect(f.state.peakRun).toBe(1); expect(f.state.peakDevice).toBe(1); expect(f.violations).toEqual([]);
  await info.attach('writes', { body: JSON.stringify(f.writes), contentType: 'application/json' });
});

test('UI-选择拒绝与CAS：迟到成功不掩盖新失败，放弃取消与灵庙失败不改草稿', async ({ page }) => {
  const f = await fixture(page), old = deferred();
  f.state.handlers.set('POST /api/v1/profile/effective', async (route, body) => {
    if (body.task_id === 'B') { await old.promise; await route.fulfill({ json: { ...f.state.profile, profile: { ...f.state.profile.profile, FARM_TARGET: 'B' } } }); }
    else await route.fulfill({ status: 500, json: { error_code: 'LATEST_FAILED' } });
  });
  await home(page);
  const target = page.getByRole('combobox', { name: '任务目标', exact: true });
  await target.selectOption('B'); await target.selectOption('C');
  await expect(page.getByRole('alert')).toContainText('LATEST_FAILED'); old.resolve();
  await expect(target).toHaveValue('Scorpionesses'); await expect(page.getByLabel('旅店间隔')).toHaveValue('1');
  await page.getByLabel('旅店间隔').fill('4');
  page.once('dialog', dialog => dialog.dismiss()); await target.selectOption('B');
  await expect(target).toHaveValue('Scorpionesses'); await expect(page.getByLabel('旅店间隔')).toHaveValue('4');
  page.once('dialog', dialog => dialog.dismiss()); await page.getByRole('button', { name: '流程编辑', exact: true }).click();
  await expect(page.getByLabel('旅店间隔')).toHaveValue('4');
  await page.getByRole('button', { name: '重载', exact: true }).click();
  f.state.handlers.set('POST /api/v1/profile/effective', async route => { await route.fulfill({ json: { ...f.state.profile, revision: 'outside-change' } }); });
  await target.selectOption('C'); await expect(page.getByRole('alert')).toContainText('PROFILE_REVISION_CONFLICT'); await expect(target).toHaveValue('Scorpionesses');
  await page.getByRole('button', { name: '设备与高级', exact: true }).click();
  const before = await page.getByLabel('灵庙切换记录').inputValue();
  await page.getByRole('button', { name: '灵庙已刷新，切换目标' }).click();
  await expect(page.getByLabel('灵庙切换记录')).toHaveValue(before); await expect(target).toHaveValue('Scorpionesses');
  await page.getByRole('button', { name: '重新读取配置' }).click();
  await expect(page.getByRole('alert')).toHaveCount(0); await expect(target).toHaveValue('Scorpionesses');
  expect(f.violations).toEqual([]);
});

test('UI-准备与陈旧：无run_id停止，跨页保持请求，超过5秒不伪造空闲', async ({ page }) => {
  const f = await fixture(page), posted = deferred();
  f.state.handlers.set('POST /api/v1/workflows/closure-flow/run', async (route, body) => {
    f.state.run = { state: 'Idle', quiescent: true, submission: { state: 'preparing', request_id: body.request_id } };
    await posted.promise; await route.fulfill({ json: { accepted: true, request_id: body.request_id } });
  });
  f.state.handlers.set('POST /api/v1/runs/current/stop', async (route, body) => {
    expect(body.request_id).toBe(f.writes.find(w => w.url.endsWith('/run'))!.request_id);
    await route.fulfill({ json: { accepted: true } });
  });
  await home(page); await page.getByRole('button', { name: '流程编辑', exact: true }).click();
  await page.getByRole('button', { name: '运行', exact: true }).click();
  await page.getByRole('button', { name: '工作台', exact: true }).click();
  await page.getByRole('button', { name: '停止', exact: true }).click();
  await expect.poll(() => f.writes.filter(w => w.url.endsWith('/stop')).length).toBe(1);
  posted.resolve(); await expect(page.getByRole('button', { name: '开始任务' })).toBeDisabled();
  const hold = deferred(); f.state.holdRun = () => hold.promise;
  await page.getByRole('button', { name: '刷新运行状态' }).click();
  await expect(page.getByLabel('当前运行会话')).toContainText('状态未知', { timeout: 7500 });
  await expect(page.getByRole('button', { name: '停止', exact: true })).toBeEnabled();
  await expect(page.getByRole('button', { name: '开始任务' })).toBeDisabled();
  f.state.run = { state: 'UserStopped', quiescent: true, submission: { state: 'cancelled', request_id: f.writes[0].request_id } };
  f.state.holdRun = undefined; hold.resolve(); await page.getByRole('button', { name: '刷新运行状态' }).click();
  await expect(page.getByRole('button', { name: '开始任务' })).toBeEnabled();
  expect(f.writes.filter(w => w.url.endsWith('/run'))).toHaveLength(1); expect(f.state.peakRun).toBe(1); expect(f.violations).toEqual([]);
});

test('UI-附加写入锁：清覆盖、导入、同步、提取及删除失败保留文档', async ({ page }) => {
  const f = await fixture(page); f.state.profile.profile.TASK_SPECIFIC_CONFIG = true;
  f.state.flow.nodes[0].position = { x: 350, y: 50 };
  (f.state.flow as any).nodes.unshift({ id: 'wait', type: 'editor', position: { x: 0, y: 50 }, data: { label: '等待', node_type: 'wait', parameters: { duration_ms: 10 } } });
  (f.state.flow as any).edges = [{ id: 'wait-done', source: 'wait', target: 'done', sourceHandle: 'success', data: { kind: 'sequence', order: 0 } }];
  f.state.flow.entry_node_id = 'wait';
  const stop = async (route: Route) => { await route.fulfill({ json: { accepted: true } }); };
  f.state.handlers.set('POST /api/v1/runs/current/stop', stop);
  let gate = deferred();
  const fail = async (route: Route) => { await gate.promise; await route.fulfill({ status: 409, json: { error_code: 'CLOSURE_CONFLICT' } }); };
  f.state.handlers.set('PUT /api/v1/profile', fail);
  await home(page); await page.getByRole('button', { name: '清除当前任务覆盖' }).click();
  await expect(page.getByLabel('旅店间隔')).toBeDisabled(); await expect(page.getByRole('button', { name: '流程编辑', exact: true })).toBeDisabled();
  gate.resolve(); await expect(page.getByRole('alert')).toContainText('CLOSURE_CONFLICT'); await expect(page.getByLabel('使用任务专用配置')).toBeChecked();
  (f.state.flow as any).builtin_status = 'update_available';
  await page.getByRole('button', { name: '流程编辑', exact: true }).click();
  const checkLock = async () => {
    await expect(page.getByLabel('流程说明')).toBeDisabled();
    await expect(page.getByRole('button', { name: '工作台', exact: true })).toBeDisabled();
    await expect(page.getByRole('button', { name: '停止', exact: true })).toBeEnabled();
    gate.resolve(); await expect(page.getByLabel('流程说明')).toBeEnabled();
    await expect(page.getByLabel('流程名称')).toHaveValue('离线流程');
  };
  gate = deferred(); f.state.handlers.set('POST /api/v1/workflows/from-task', fail);
  await page.getByRole('combobox', { name: '复制现有任务' }).selectOption('Scorpionesses');
  await page.getByRole('button', { name: '生成编辑副本' }).click(); await checkLock();
  f.state.handlers.set('GET /api/v1/workflows/closure-flow/builtin', async route => { await route.fulfill({ json: {
    flow_id: 'closure-flow', status: 'update_available', local_revision: 'flow-1', builtin_revision: 'flow-2', current: f.state.flow, builtin: f.state.flow, affected_references: [] } }); });
  await page.getByRole('button', { name: '内置定义', exact: true }).click();
  gate = deferred(); f.state.handlers.set('POST /api/v1/workflows/closure-flow/builtin', fail);
  page.once('dialog', dialog => dialog.accept()); await page.getByRole('button', { name: '同步此定义' }).click(); await checkLock();
  await page.getByRole('button', { name: '关闭比较' }).click();
  await page.locator('.vue-flow__node[data-id="wait"]').click();
  gate = deferred(); f.state.handlers.set('POST /api/v1/workflows', fail);
  page.once('dialog', dialog => dialog.accept('受控公共块'));
  await page.getByRole('button', { name: '提取选中步骤为公共块' }).click(); await checkLock();
  gate = deferred(); f.state.handlers.set('DELETE /api/v1/workflows/closure-flow', fail);
  page.once('dialog', dialog => dialog.accept()); await page.getByRole('button', { name: '删除流程' }).click(); await checkLock();
  expect(f.writes.map(w => w.method + ' ' + w.url)).toEqual(['PUT /api/v1/profile', 'POST /api/v1/workflows/from-task', 'POST /api/v1/workflows/closure-flow/builtin', 'POST /api/v1/workflows', 'DELETE /api/v1/workflows/closure-flow']);
  expect(f.violations).toEqual([]);
});

test('UI-功能保留：三组配置、恢复反向布尔、策略列表与隐藏字段保存重开', async ({ page }, info) => {
  const f = await fixture(page);
  f.state.handlers.set('PUT /api/v1/profile', async (route, body) => { f.state.profile = { ...body, revision: 'saved-all-controls' }; await route.fulfill({ json: f.state.profile }); });
  await home(page);
  const checks: Record<string,string> = { '快速开箱': 'QUICK_DISARM_CHEST', '刚入地下城恢复': 'RECOVER_WHEN_BEGINNING', '主动旅店休息': 'ACTIVE_REST', '每六小时重组队伍': 'RE_ASSEMBLE_PARTY' };
  for (const label of Object.keys(checks)) await page.getByRole('checkbox', { name: label, exact: true }).check();
  await page.getByLabel('战后恢复', { exact: true }).uncheck(); await page.getByLabel('开箱后恢复', { exact: true }).uncheck();
  await page.getByLabel('旅店间隔').fill('3');
  await page.getByRole('checkbox', { name: '行动栏头像识别特殊敌人' }).check();
  await expect(page.getByRole('combobox', { name: '头像模板' })).toHaveValue('retained-custom');
  await page.getByRole('checkbox', { name: '行动栏头像识别特殊敌人' }).uncheck();
  await page.getByRole('button', { name: '战斗方案', exact: true }).click();
  await expect(page.getByLabel('旅店间隔')).not.toBeVisible();
  await page.getByLabel('释放任一即视为完成').check(); await page.getByRole('button', { name: '新增角色技能' }).click();
  await expect(page.locator('.skill-row')).toHaveCount(2); await page.getByRole('button', { name: '删除技能行' }).last().click();
  await page.getByRole('button', { name: '新建方案', exact: true }).click(); await page.getByLabel('方案名称').fill('temporary');
  page.once('dialog', dialog => dialog.accept()); await page.getByRole('button', { name: '删除方案 temporary' }).click();
  await page.getByLabel('查找方案').fill('方案A'); await expect(page.getByRole('option', { name: '方案A 1 项' })).toBeVisible();
  await page.getByRole('button', { name: '设备与高级', exact: true }).click();
  for (const [label,key] of Object.entries({ '自动要钱':'ACTIVE_BEG_MONEY', '豪华房':'ACTIVE_ROYALSUITE_REST', '凯旋':'ACTIVE_TRIUMPH', '美丽矿石的真相':'ACTIVE_BEAUTIFUL_ORE', '因果调整':'ACTIVE_CSC', '空气墙绕行':'BYPASS_THE_WALL', '重启后自动启动 Clash 并打开 VPN':'AUTO_START_CLASH' })) {
    checks[label] = key; await page.getByRole('checkbox', { name: label, exact: true }).check();
  }
  await page.getByLabel('实例编号').fill('12'); await page.getByLabel('定位失败阈值').fill('26'); await page.getByLabel('重启阈值').fill('11');
  await page.getByLabel('官网领取记录').fill('2026-09-27'); await page.getByLabel('灵庙切换记录').fill('2026-09-27');
  await expect(page.getByRole('link', { name: '领取 50 钻（旧）' })).toHaveAttribute('href','https://store.wizardry.info/');
  await expect(page.getByRole('link', { name: '领取 50 钻（新）' })).toHaveAttribute('href','https://webstore.wizardry.info/');
  await page.getByRole('button', { name: '保存配置' }).click(); await expect(page.getByRole('button', { name: '保存配置' })).toBeDisabled();
  const saved = f.writes.find(w => w.method === 'PUT')!.body.profile;
  for (const key of Object.values(checks)) expect(saved[key], key).toBe(true);
  expect(saved.SKIP_COMBAT_RECOVER).toBe(true); expect(saved.SKIP_CHEST_RECOVER).toBe(true);
  expect(saved.STRATEGY).toHaveLength(1); expect(saved.STRATEGY[0].skill_settings).toHaveLength(1); expect(saved.STRATEGY[0].complete_one_as_all).toBe(true);
  expect(saved.TASK_POINT_STRATEGY.special_combat.portrait_image).toBe('retained-custom');
  expect(saved.UNKNOWN_KEEP).toEqual({ nested: ['unchanged'] });
  await page.reload(); await expect(page.getByLabel('旅店间隔')).toHaveValue('3');
  await expect(page.getByLabel('战后恢复', { exact: true })).not.toBeChecked();
  await page.getByRole('button', { name: '设备与高级', exact: true }).click(); await expect(page.getByLabel('实例编号')).toHaveValue('12');
  expect(f.violations).toEqual([]); await info.attach('saved-fields', { body: JSON.stringify(saved), contentType: 'application/json' });
});
