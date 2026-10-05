import { test, expect } from '@playwright/test';

test('巨人悬赏：第三章任务选取与独立循环设置', async ({ page, request }) => {
  expect(process.env.WVD_STRATEGY_ISOLATED).toBe('1');
  const base = 'http://127.0.0.1:18758';
  const blocked: string[] = [];
  await page.route('**/*', async route => {
    const req = route.request(), url = new URL(req.url());
    if (url.origin === base && (req.method() === 'GET' ||
      (req.method() === 'POST' && url.pathname === '/api/v1/profile/effective') ||
      (req.method() === 'PUT' && url.pathname === '/api/v1/profile'))) await route.continue();
    else { blocked.push(req.method() + ' ' + url.pathname); await route.abort(); }
  });
  const entry = await (await request.get(base + '/api/v1/profile')).json();
  if (entry.profile.FARM_TARGET === 'GiantBounty' || entry.profile.TASK_SPECIFIC_CONFIG) {
    const reset = await request.put(base + '/api/v1/profile', { data: {
      revision: entry.revision, scope: 'task', profile: {
        FARM_TARGET: 'Scorpionesses', FARM_TARGET_TEXT: '[悬赏]蝎女', TASK_SPECIFIC_CONFIG: false
      }
    } });
    expect(reset.ok()).toBe(true);
  }
  const initial = await (await request.get(base + '/api/v1/profile')).json();
  await page.goto(base);
  await page.getByRole('combobox', { name: '任务类别', exact: true }).selectOption('主线前三章');
  await page.getByRole('combobox', { name: '任务目标', exact: true }).selectOption('GiantBounty');
  await expect(page.getByRole('combobox', { name: '循环模式', exact: true })).toBeVisible();
  await page.getByRole('combobox', { name: '循环模式', exact: true }).selectOption('count');
  await page.getByLabel('循环次数', { exact: true }).fill('5');
  await page.getByRole('button', { name: '保存任务设置', exact: true }).click();
  await expect(page.getByText('已保存任务设置', { exact: true })).toBeVisible();
  const saved = await (await request.get(base + '/api/v1/profile')).json();
  expect(saved.profile.FARM_TARGET).toBe('GiantBounty');
  expect(saved.profile.FARM_TARGET_TEXT).toBe('[悬赏]巨人');
  expect(saved.profile.STRATEGY).toEqual(initial.profile.STRATEGY);
  expect(saved.profile.ACTIVE_TRIUMPH).toBe(initial.profile.ACTIVE_TRIUMPH);
  expect(saved.profile.ACTIVE_BEAUTIFUL_ORE).toBe(initial.profile.ACTIVE_BEAUTIFUL_ORE);
  await page.reload();
  await expect(page.getByRole('combobox', { name: '任务目标', exact: true })).toHaveValue('GiantBounty');
  await expect(page.getByRole('combobox', { name: '循环模式', exact: true })).toBeVisible();
  await page.screenshot({ path: 'test-results/giant-bounty-task.png', fullPage: true });
  expect(blocked).toEqual([]);
});

const base = 'http://127.0.0.1:18758';
// 验证真实CAS接口和落盘范围，绝不连接设备或运行游戏。
test('分区保存：服务端隔离、其它草稿保留、引用重绑定与刷新保护', async ({ page, request }, info) => {
  expect(process.env.WVD_STRATEGY_ISOLATED).toBe('1');
  const blocked: string[] = [];
  await page.route('**/*', async route => {
    const req = route.request(), url = new URL(req.url());
    if (url.origin === base && (req.method() === 'GET' || (req.method() === 'PUT' && url.pathname === '/api/v1/profile'))) await route.continue();
    else { blocked.push(req.method() + ' ' + url.pathname); await route.abort(); }
  });
  const entry = await (await request.get(base + '/api/v1/profile')).json();
  // 两个视口顺序共用一次隔离服务，各例先明确回到默认配置上下文。
  if (entry.profile.TASK_SPECIFIC_CONFIG) {
    const reset = await request.put(base + '/api/v1/profile', { data: {
      revision: entry.revision, scope: 'task', profile: { FARM_TARGET: entry.profile.FARM_TARGET,
        FARM_TARGET_TEXT: entry.profile.FARM_TARGET_TEXT, TASK_SPECIFIC_CONFIG: false }
    } });
    expect(reset.ok()).toBe(true);
  }
  const initial = await (await request.get(base + '/api/v1/profile')).json();
  const catalog = await (await request.get(base + '/api/v1/catalog')).json();
  const rest = Number(initial.profile.REST_INTERVEL) + 3;
  const index = Number(initial.profile.EMU_INDEX) + 10;
  await page.goto(base);
  await page.getByLabel('旅店间隔', { exact: true }).fill(String(rest));
  await page.getByRole('button', { name: '设备与高级', exact: true }).click();
  await page.getByLabel('实例编号', { exact: true }).fill(String(index));
  await page.getByRole('combobox', { name: '日志级别', exact: true }).selectOption('debug');
  await page.getByRole('button', { name: '常用参数', exact: true }).click();
  await expect(page.locator('.battle-section .strategy-workspace')).toBeVisible();
  const firstGroup = initial.profile.STRATEGY[0].group_name;
  const renamed = `${firstGroup}-局部保存-${info.project.name}`;
  await page.getByRole('button', { name: '重命名', exact: true }).click();
  await page.getByLabel('方案名称', { exact: true }).fill(renamed);
  await page.getByRole('button', { name: '确认重命名', exact: true }).click();
  await page.getByRole('button', { name: '保存战斗方案', exact: true }).click();
  await expect(page.getByText('已保存战斗方案', { exact: true })).toBeVisible();
  const combat = await (await request.get(base + '/api/v1/profile')).json();
  expect(combat.profile.STRATEGY[0].group_name).toBe(renamed);
  expect(combat.profile.REST_INTERVEL).toBe(initial.profile.REST_INTERVEL);
  expect(combat.profile.EMU_INDEX).toBe(initial.profile.EMU_INDEX);
  expect(combat.logging).toEqual(initial.logging);
  expect(combat.profile.FARM_TARGET).toBe(initial.profile.FARM_TARGET);
  if (initial.profile.DEFAULT_OVERALL_STRATEGY === firstGroup) expect(combat.profile.DEFAULT_OVERALL_STRATEGY).toBe(renamed);
  await page.getByRole('button', { name: '常用参数', exact: true }).click();
  await expect(page.getByLabel('旅店间隔', { exact: true })).toHaveValue(String(rest));
  await expect(page.getByRole('button', { name: '保存常用参数', exact: true })).toBeEnabled();
  await page.getByRole('button', { name: '保存常用参数', exact: true }).click();
  await expect(page.getByText('已保存常用参数', { exact: true })).toBeVisible();
  const common = await (await request.get(base + '/api/v1/profile')).json();
  expect(common.profile.REST_INTERVEL).toBe(rest);
  expect(common.profile.STRATEGY).toEqual(combat.profile.STRATEGY);
  expect(common.profile.EMU_INDEX).toBe(initial.profile.EMU_INDEX);
  expect(common.logging).toEqual(initial.logging);
  await page.getByRole('button', { name: '设备与高级', exact: true }).click();
  await expect(page.getByLabel('实例编号', { exact: true })).toHaveValue(String(index));
  await expect(page.getByRole('combobox', { name: '日志级别', exact: true })).toHaveValue('debug');
  await page.getByRole('button', { name: '保存设备与高级', exact: true }).click();
  await expect(page.getByText('已保存设备与高级', { exact: true })).toBeVisible();
  const advanced = await (await request.get(base + '/api/v1/profile')).json();
  expect(advanced.profile.EMU_INDEX).toBe(index);
  expect(advanced.logging.level).toBe('debug');
  expect(advanced.profile.REST_INTERVEL).toBe(rest);
  expect(advanced.profile.STRATEGY).toEqual(combat.profile.STRATEGY);

  const payload = { revision: advanced.revision, scope: 'combat', profile: Object.fromEntries(catalog.profile_save_sections.combat.map((key: string) => [key, advanced.profile[key]])), strategy_renames: {} };
  const crossed = await request.put(base + '/api/v1/profile', { data: { ...payload, profile: { ...payload.profile, EMU_INDEX: 999 } } });
  expect(crossed.ok()).toBe(false);
  expect(JSON.stringify(await crossed.json())).toContain('PROFILE_SCOPE_FIELDS_INVALID');
  const stale = await request.put(base + '/api/v1/profile', { data: { ...payload, revision: initial.revision } });
  expect(stale.ok()).toBe(false);
  expect(JSON.stringify(await stale.json())).toContain('PROFILE_CONFLICT');
  expect(await (await request.get(base + '/api/v1/profile')).json()).toEqual(advanced);

  await page.getByRole('checkbox', { name: '使用任务专用配置', exact: true }).check();
  await page.getByRole('button', { name: '保存任务设置', exact: true }).click();
  await expect(page.getByText('已保存任务设置', { exact: true })).toBeVisible();
  const task = await (await request.get(base + '/api/v1/profile')).json();
  expect(task.profile.TASK_SPECIFIC_CONFIG).toBe(true);
  const { TASK_SPECIFIC_CONFIG: _before, ...oldFields } = advanced.profile;
  const { TASK_SPECIFIC_CONFIG: _after, ...newFields } = task.profile;
  expect(newFields).toEqual(oldFields);
  expect(task.logging).toEqual(advanced.logging);
  await page.reload();
  await expect(page.getByLabel('旅店间隔', { exact: true })).toHaveValue(String(rest));
  await expect(page.getByRole('button', { name: '保存常用参数', exact: true })).toBeDisabled();
  await page.getByRole('button', { name: '常用参数', exact: true }).click();
  await expect(page.locator('.strategy-title')).toHaveText(renamed);
  await expect(page.getByRole('button', { name: '保存战斗方案', exact: true })).toBeDisabled();
  // 已有任务覆盖不能在保存新任务目标时被复制到另一个任务。
  const commonPayload = Object.fromEntries(catalog.profile_save_sections.common.map((key: string) => [key, task.profile[key]]));
  const overridden = await request.put(base + '/api/v1/profile', { data: {
    revision: task.revision, scope: 'common', profile: { ...commonPayload, REST_INTERVEL: rest + 4 }
  } });
  expect(overridden.ok()).toBe(true);
  const withOverride = await overridden.json();
  const otherTask = catalog.tasks.find((item: { id: string }) => item.id !== task.profile.FARM_TARGET);
  const selected = await (await request.post(base + '/api/v1/profile/effective', { data: { task_id: otherTask.id } })).json();
  const switched = await request.put(base + '/api/v1/profile', { data: {
    revision: withOverride.revision, scope: 'task', profile: Object.fromEntries(catalog.profile_save_sections.task.map((key: string) => [key, selected.profile[key]]))
  } });
  expect(switched.ok()).toBe(true);
  const switchedValue = await switched.json();
  expect(switchedValue.profile.FARM_TARGET).toBe(otherTask.id);
  expect(switchedValue.profile.REST_INTERVEL).toBe(selected.profile.REST_INTERVEL);
  expect(switchedValue.profile.REST_INTERVEL).not.toBe(rest + 4);
  expect(switchedValue.logging).toEqual(task.logging);
  expect(blocked).toEqual([]);
});
