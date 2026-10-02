import { test, expect } from '@playwright/test';

// 只连接专用临时 profile 的真实服务，不允许启动任务或连接设备。
const base = 'http://127.0.0.1:18758';
test('战斗方案：六个友方位置、两种频次及保存重载', async ({ page, request }, info) => {
  expect(process.env.WVD_STRATEGY_ISOLATED).toBe('1');
  const blocked: string[] = [];
  await page.route('**/*', async route => {
    const req = route.request(), url = new URL(req.url());
    const safe = url.origin === base && (req.method() === 'GET' ||
      (req.method() === 'PUT' && url.pathname === '/api/v1/profile'));
    if (!safe) { blocked.push(req.method() + ' ' + url.pathname); await route.abort(); }
    else await route.continue();
  });
  const catalog = await (await request.get(base + '/api/v1/catalog')).json();
  expect(catalog.skill_targets.map((v: any) => v.value)).toEqual(['左上角色', '中上角色', '右上角色', '左下角色', '中下角色', '右下角色']);
  expect(catalog.skill_frequencies.map((v: any) => v.label)).toEqual(['用完后移除', '重复该动作']);
  await page.goto(base);
  await page.getByRole('button', { name: '战斗方案', exact: true }).click();
  await page.getByRole('button', { name: '新建方案', exact: true }).click();
  const name = '频次验收-' + info.project.name;
  await page.getByLabel('方案名称', { exact: true }).fill(name);
  await page.getByRole('button', { name: '新增角色技能', exact: true }).click();
  await expect(page.getByLabel('技能目标', { exact: true })).toHaveValue('左上角色');
  await expect(page.getByLabel('技能频次', { exact: true })).toHaveValue('用完后移除');
  await expect(page.getByLabel('技能目标', { exact: true }).locator('option')).toHaveCount(6);
  await expect(page.getByLabel('技能频次', { exact: true }).locator('option')).toHaveCount(2);
  await page.getByLabel('技能目标', { exact: true }).selectOption('右下角色');
  await page.getByLabel('技能频次', { exact: true }).selectOption('重复');
  await page.getByRole('button', { name: '保存配置', exact: true }).click();
  await expect(page.getByText('配置已由服务端保存')).toBeVisible();
  const saved = await (await request.get(base + '/api/v1/profile')).json();
  const row = saved.profile.STRATEGY.find((v: any) => v.group_name === name).skill_settings[0];
  expect(row.target_var).toBe('右下角色'); expect(row.freq_var).toBe('重复');
  await page.reload();
  await page.getByRole('button', { name: '战斗方案', exact: true }).click();
  await page.getByRole('option', { name: name + ' 1 项', exact: true }).click();
  await expect(page.getByLabel('技能目标', { exact: true })).toHaveValue('右下角色');
  await expect(page.getByLabel('技能频次', { exact: true })).toHaveValue('重复');
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(true);
  await page.getByLabel('技能频次', { exact: true }).scrollIntoViewIfNeeded();
  await page.screenshot({ path: info.outputPath('strategy-frequency.png'), fullPage: true });
  expect(blocked).toEqual([]);
});
