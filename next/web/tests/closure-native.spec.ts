import { test, expect } from '@playwright/test';
import fs from 'node:fs';
import path from 'node:path';

const base = 'http://127.0.0.1:18755';
test('API-持久化：正式候选保存、CAS、隐藏引用、作者文档与服务重开', async ({ page, request }, info) => {
  const forbidden: string[] = [];
  const safe = (method: string, path: string) => method === 'GET' && /^\/api\/v1\/(version|profile|catalog|device|runs\/current|workflows(?:\/[^/]+)?)$/.test(path) ||
    ['PUT /api/v1/profile', 'POST /api/v1/profile/effective', 'POST /api/v1/workflows', 'POST /api/v1/recognition/probe'].includes(`${method} ${path}`) ||
    method === 'PUT' && /^\/api\/v1\/workflows\/[^/]+$/.test(path);
  await page.route('**/*', async route => {
    const url = new URL(route.request().url()), method = route.request().method();
    if (url.origin !== base || (url.pathname.startsWith('/api/') && !safe(method, url.pathname))) {
      forbidden.push(`${method} ${url.href}`); await route.abort('blockedbyclient'); return;
    }
    await route.continue();
  });
  async function api(method: 'GET' | 'PUT' | 'POST', path: string, data?: unknown) {
    expect(safe(method, path)).toBe(true);
    if (path === '/api/v1/recognition/probe') expect((data as any)?.image_base64).toBeTruthy();
    const response = await request.fetch(base + path, { method, data });
    expect(response.ok(), `${method} ${path}: ${await response.text()}`).toBe(true);
    return response.json();
  }
  const original = await api('GET', '/api/v1/profile');
  const catalogue = await api('GET', '/api/v1/catalog');
  expect(catalogue.semantic_resources.length).toBeGreaterThan(0);
  expect(catalogue.templates.some((entry: any) => entry.value === 'Inn.png')).toBe(true);
  // 用正式模板生成本地上传图片，仅验证资源消费链，不获取任何设备帧。
  const template = fs.readFileSync(path.join(process.env.WVD_CLOSURE_ROOT!, 'candidate/pack/image/Inn.png')).toString('base64');
  const uploaded = await page.evaluate(async data => {
    const image = new Image(); image.src = `data:image/png;base64,${data}`; await image.decode();
    const canvas = document.createElement('canvas'); canvas.width = 900; canvas.height = 1600;
    const context = canvas.getContext('2d')!; context.fillStyle = '#000'; context.fillRect(0, 0, 900, 1600);
    context.drawImage(image, 60, 450);
    return { image_base64: canvas.toDataURL('image/png').split(',')[1], width: image.width, height: image.height };
  }, template);
  const probe = await api('POST', '/api/v1/recognition/probe', {
    image_base64: uploaded.image_base64, resource_locale: 'en',
    recognition: { mode: 'template', image: 'Inn', threshold: 0.8, roi: [0, 0, 900, 1600] },
  });
  expect(probe.outcome).toBe('Hit');
  expect(probe.box).toEqual([60, 450, uploaded.width, uploaded.height]);
  await info.attach('formal-upload-probe', { body: JSON.stringify({ probe, templates: catalogue.templates.length, semantic_resources: catalogue.semantic_resources.length }), contentType: 'application/json' });
  const target = catalogue.tasks.find((task: any) => task.id === 'Scorpionesses');
  expect(target).toBeTruthy();
  const profile = structuredClone(original.profile);
  profile.FARM_TARGET = target.id; profile.FARM_TARGET_TEXT = target.name;
  profile.TASK_SPECIFIC_CONFIG = false;
  profile.STRATEGY = [{ group_name: 'closure-A', skill_settings: [], complete_one_as_all: false }];
  profile.DEFAULT_OVERALL_STRATEGY = 'closure-A';
  profile.TASK_POINT_STRATEGY = { overall_strategy: 'closure-A', task_point: { '0': 'closure-A' },
    special_combat: { skull: false, portrait: false, portrait_image: 'combat_scorpion_portrait', normal_strategy: 'closure-A', special_strategy: 'closure-A' },
    closure_hidden: { preserved: true } };
  // 正式 profile 顶层固定33字段，未知内容保留在既有扩展对象内，不改后端Schema。
  let saved = await api('PUT', '/api/v1/profile', { ...original, profile });
  saved = await api('PUT', '/api/v1/profile', { ...saved, profile: { ...saved.profile, TASK_SPECIFIC_CONFIG: true } });
  const selected = await api('POST', '/api/v1/profile/effective', { task_id: target.id });
  expect(selected.task_override_active).toBe(true);
  const other = catalogue.tasks.find((task: any) => task.id !== target.id && task.type === 'dungeon');
  expect(other).toBeTruthy();
  const switched = await api('POST', '/api/v1/profile/effective', { task_id: other.id });
  // 保存另一个任务，再改方案名，验证未展示的蝎女覆盖引用也由正式 API 更新。
  saved = await api('PUT', '/api/v1/profile', { ...saved, profile: { ...switched.profile, TASK_SPECIFIC_CONFIG: true } });
  const renamed = structuredClone(saved.profile);
  renamed.STRATEGY[0].group_name = 'closure-B'; renamed.DEFAULT_OVERALL_STRATEGY = 'closure-B';
  if (renamed.TASK_POINT_STRATEGY?.overall_strategy === 'closure-A') renamed.TASK_POINT_STRATEGY.overall_strategy = 'closure-B';
  for (const key of Object.keys(renamed.TASK_POINT_STRATEGY.task_point ?? {}))
    if (renamed.TASK_POINT_STRATEGY.task_point[key] === 'closure-A') renamed.TASK_POINT_STRATEGY.task_point[key] = 'closure-B';
  for (const key of ['normal_strategy', 'special_strategy'])
    if (renamed.TASK_POINT_STRATEGY.special_combat?.[key] === 'closure-A') renamed.TASK_POINT_STRATEGY.special_combat[key] = 'closure-B';
  saved = await api('PUT', '/api/v1/profile', { ...saved, profile: renamed, strategy_renames: { 'closure-A': 'closure-B' } });
  const hidden = await api('POST', '/api/v1/profile/effective', { task_id: target.id });
  expect(hidden.profile.TASK_POINT_STRATEGY.overall_strategy).toBe('closure-B');
  expect(hidden.profile.TASK_POINT_STRATEGY.task_point['0']).toBe('closure-B');
  expect(hidden.profile.TASK_POINT_STRATEGY.special_combat.portrait).toBe(false);
  expect(hidden.profile.TASK_POINT_STRATEGY.special_combat.portrait_image).toBe('combat_scorpion_portrait');
  expect(hidden.profile.TASK_POINT_STRATEGY.closure_hidden).toEqual({ preserved: true });
  const conflict = await request.put(base + '/api/v1/profile', { data: { ...saved, revision: original.revision } });
  // 本项目的 error_reply 统一返回400；以真实冲突码及保存内容不变证明CAS，不改HTTP契约。
  expect(conflict.status()).toBe(400); expect((await conflict.json()).error_code).toBe('PROFILE_CONFLICT');
  expect(await api('GET', '/api/v1/profile')).toEqual(saved);
  await page.goto(base + '/');
  await expect(page.getByRole('heading', { name: '工作台', exact: true })).toBeVisible();
  await page.getByRole('button', { name: '设备与高级', exact: true }).click();
  await page.getByLabel('实例编号').fill('13');
  await page.getByRole('button', { name: '保存配置' }).click();
  await expect(page.getByText('配置已由服务端保存')).toBeVisible();
  const profileBefore = await api('GET', '/api/v1/profile');
  expect(profileBefore.profile.EMU_INDEX).toBe(13);
  const workflows = await api('GET', '/api/v1/workflows');
  const id = 'wheel-reset';
  expect(workflows.workflows.some((flow: any) => flow.id === id)).toBe(true);
  const before = await api('GET', `/api/v1/workflows/${id}`);
  await page.getByRole('button', { name: '流程编辑', exact: true }).click();
  await expect(page.getByLabel('流程名称')).toBeVisible();
  await page.getByRole('combobox', { name: '流程', exact: true }).selectOption(id);
  await page.getByLabel('流程说明').fill('closure-persistence-marker');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect(page.getByText('流程已由服务端校验并保存')).toBeVisible();
  const after = await api('GET', `/api/v1/workflows/${id}`);
  for (const key of ['nodes', 'edges', 'entry_node_id', 'layout', 'viewport', 'events', 'checks', 'interface', 'slots'])
    expect(after[key], key).toEqual(before[key]);
  expect(after.description).toBe('closure-persistence-marker');
  expect(after.nodes.some((node: any) => node.data.node_type === 'slot')).toBe(true);
  const handoffBefore = await api('GET', '/api/v1/workflows/combat-select-level');
  expect(handoffBefore.interface.handoffs).toContain('unavailable');
  await page.getByRole('combobox', { name: '流程', exact: true }).selectOption('combat-select-level');
  await expect(page.getByLabel('流程名称')).toHaveValue(handoffBefore.name);
  await page.getByLabel('流程说明').fill('closure-handoff-marker');
  const handoffSaved = page.waitForResponse(response => response.url() === base + '/api/v1/workflows/combat-select-level' && response.request().method() === 'PUT');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  expect((await handoffSaved).ok()).toBe(true);
  // 保存还会刷新作者目录；等编辑锁释放后再发独立核对请求，避免夹具争抢仓库锁。
  await expect(page.getByLabel('流程说明')).toBeEnabled();
  await expect(page.getByText('流程已由服务端校验并保存')).toBeVisible();
  await expect(page.getByRole('button', { name: '保存', exact: true })).toBeDisabled();
  const handoffAfter = await api('GET', '/api/v1/workflows/combat-select-level');
  for (const key of ['nodes', 'edges', 'layout', 'viewport', 'events', 'checks', 'interface', 'slots']) expect(handoffAfter[key], key).toEqual(handoffBefore[key]);
  const restart = await request.post('http://127.0.0.1:18754/__closure/restart-native'); expect(restart.ok()).toBe(true);
  expect(await api('GET', '/api/v1/profile')).toEqual(profileBefore);
  expect(await api('GET', `/api/v1/workflows/${id}`)).toEqual(after);
  expect(await api('GET', '/api/v1/workflows/combat-select-level')).toEqual(handoffAfter);
  await page.reload(); await page.getByRole('button', { name: '设备与高级', exact: true }).click();
  await expect(page.getByLabel('实例编号')).toHaveValue('13');
  expect(forbidden).toEqual([]);
  await info.attach('formal-api-before-after', { body: JSON.stringify({ original, profileBefore, hidden, before, after }), contentType: 'application/json' });
});
