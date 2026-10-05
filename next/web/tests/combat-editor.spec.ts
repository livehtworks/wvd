import { test, expect, type Locator, type Page } from '@playwright/test';
import { resolve } from 'node:path';

const base = 'http://127.0.0.1:18758';
test('战斗列表紧凑布局：宽屏和放大等效视口只读核对', async ({ page, request }) => {
  expect(process.env.WVD_STRATEGY_ISOLATED).toBe('1');
  const forbidden: string[] = [];
  await page.route('**/*', async route => {
    const req = route.request();
    if (new URL(req.url()).origin === base && req.method() === 'GET') await route.continue();
    else { forbidden.push(req.method() + ' ' + req.url()); await route.abort(); }
  });
  const original = await (await request.get(base + '/api/v1/profile')).json();
  const group = [...original.profile.STRATEGY].sort((a, b) => b.skill_settings.length - a.skill_settings.length)[0];
  await page.goto(base);
  await page.getByRole('button', { name: '常用参数', exact: true }).click();
  const tabs = page.getByRole('navigation', { name: '工作台设置' });
  await expect(tabs.getByRole('button')).toHaveCount(2);
  await expect(tabs.getByRole('button', { name: '战斗方案', exact: true })).toHaveCount(0);
  await expect(page.locator('.battle-section .combat-section')).toBeVisible();
  await page.getByRole('option', { name: `${group.group_name} ${group.skill_settings.length} 项`, exact: true }).click();
  // Browser zoom reduces the CSS viewport; these are its equivalent layout sizes.
  for (const width of [1920, 1280, 960, 390]) {
    await page.setViewportSize({ width, height: 1000 });
    const row = page.locator('.skill-row').first();
    const sizes = await row.locator('select').evaluateAll(nodes => nodes.map(node => node.getBoundingClientRect().width));
    expect(sizes).toEqual([187, 146, 56, 112, 140]);
    expect((await row.boundingBox())!.height).toBeLessThanOrEqual(36);
    expect(await page.locator('.strategy-group').evaluate(node => node.getBoundingClientRect().width)).toBeLessThanOrEqual(850);
    expect(await page.locator('.combat-section').evaluate(node => node.getBoundingClientRect().width)).toBeLessThanOrEqual(1072);
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
    expect(await page.locator('.workbench-page').evaluate(node => node.getBoundingClientRect().width)).toBeLessThanOrEqual(1140);
    const checks = await page.locator('.exploration-section .check-field').evaluateAll(nodes => nodes.map(node => node.getBoundingClientRect().width));
    expect(checks.every(width => width < 190)).toBe(true);
    expect(await page.locator('.exploration-section .form-grid').evaluate(node => getComputedStyle(node).display)).toBe('flex');
    expect(await page.locator('.skill-row').evaluateAll(nodes => nodes.slice(0, 2).map(node => getComputedStyle(node).backgroundColor)))
      .toEqual(['rgb(255, 255, 255)', 'rgb(234, 240, 247)']);
    const header = await page.locator('.skill-head > span').evaluateAll(nodes => nodes.map(node => node.getBoundingClientRect().x));
    const cells = await row.locator(':scope > *').evaluateAll(nodes => nodes.map(node => node.getBoundingClientRect().x));
    expect(cells).toEqual(header);
    await expect(page.getByRole('button', { name: '开始调试', exact: true })).toBeVisible();
    await expect(page.getByRole('button', { name: '保存战斗方案', exact: true })).toBeVisible();
    await page.locator('.strategy-editor').screenshot({ path: `test-results/combat-compact-${width}.png` });
    await page.evaluate(() => window.scrollTo(0, 0));
    await page.screenshot({ path: `test-results/workbench-compact-${width}.png`, fullPage: true });
  }
  await page.getByRole('button', { name: '设备与高级', exact: true }).click();
  for (const width of [1920, 390]) {
    await page.setViewportSize({ width, height: 1000 });
    expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
    await expect(page.getByLabel('模拟器路径', { exact: true })).toBeVisible();
    if (await page.locator('.preview-empty').isVisible())
      expect((await page.locator('.preview-empty').boundingBox())!.height).toBeLessThanOrEqual(45);
    await page.evaluate(() => window.scrollTo(0, 0));
    await page.screenshot({ path: `test-results/workbench-advanced-${width}.png`, fullPage: true });
  }
  expect(forbidden).toEqual([]);
  const after = await (await request.get(base + '/api/v1/profile')).json();
  expect(after).toEqual(original);
});

async function drag(page: Page, handle: Locator, destination: Locator) {
  // Keep the complete gesture above the sticky save bar, including on narrow screens.
  await handle.evaluate(node => node.scrollIntoView({ block: 'center' }));
  const from = await handle.boundingBox(), to = await destination.boundingBox();
  expect(from).not.toBeNull(); expect(to).not.toBeNull();
  await page.mouse.move(from!.x + from!.width / 2, from!.y + from!.height / 2);
  await page.mouse.down();
  await page.mouse.move(from!.x + from!.width / 2, from!.y + from!.height / 2 + 9, { steps: 3 });
  await page.waitForTimeout(160);
  await page.mouse.move(to!.x + 16, to!.y + to!.height - 3, { steps: 15 });
  await page.waitForTimeout(180);
  await page.mouse.up();
}

test('真实分区保存：拖动排序、怪物头像及调试提交入口', async ({ page, request }, info) => {
  expect(process.env.WVD_STRATEGY_ISOLATED).toBe('1');
  const forbidden: string[] = [], debug: any[] = [];
  await page.route('**/*', async route => {
    const req = route.request(), url = new URL(req.url());
    if (url.origin === base && req.method() === 'POST' && url.pathname === '/api/v1/combat/debug') {
      debug.push(req.postDataJSON());
      // Only this UI submission is intercepted; it must not issue real game input.
      await route.fulfill({ status: 400, contentType: 'application/json', body: JSON.stringify({ error: 'COMBAT_DEBUG_NOT_IN_BATTLE' }) });
    } else if (url.origin === base && (req.method() === 'GET' || (req.method() === 'PUT' && url.pathname === '/api/v1/profile'))) {
      await route.continue();
    } else { forbidden.push(req.method() + ' ' + url.pathname); await route.abort(); }
  });
  const original = await (await request.get(base + '/api/v1/profile')).json();
  const group = original.profile.STRATEGY.find((entry: any) => entry.skill_settings.length >= 3);
  await page.goto(base);
  await page.getByRole('button', { name: '常用参数', exact: true }).click();
  await page.getByRole('option', { name: `${group.group_name} ${group.skill_settings.length} 项`, exact: true }).click();
  const rows = page.locator('.skill-row');
  const initial = await rows.getByLabel('角色', { exact: true }).evaluateAll(nodes => nodes.map(node => (node as HTMLSelectElement).value));
  const colors = await rows.evaluateAll(nodes => nodes.slice(0, 2).map(node => getComputedStyle(node).backgroundColor));
  expect(colors[0]).not.toBe(colors[1]);
  await drag(page, rows.nth(0).locator('.drag-handle'), rows.nth(2));
  const expected = initial.slice(); expected.splice(2, 0, expected.splice(0, 1)[0]);
  await expect.poll(() => rows.getByLabel('角色', { exact: true }).evaluateAll(nodes => nodes.map(node => (node as HTMLSelectElement).value))).toEqual(expected);
  expect(await rows.locator('.row-number').allTextContents()).toEqual(expected.map((_, i) => String(i + 1)));
  const names = await page.locator('.strategy-item').evaluateAll(nodes => nodes.map(node => node.getAttribute('aria-label')));
  await drag(page, page.locator('.strategy-item').nth(0).locator('.drag-handle'), page.locator('.strategy-item').nth(2));
  const expectedNames = names.slice(); expectedNames.splice(2, 0, expectedNames.splice(0, 1)[0]);
  await expect.poll(() => page.locator('.strategy-item').evaluateAll(nodes => nodes.map(node => node.getAttribute('aria-label')))).toEqual(expectedNames);
  await expect(page.locator('.strategy-title')).toHaveText(group.group_name);
  await page.getByRole('button', { name: '保存战斗方案', exact: true }).click();
  await expect(page.getByText('已保存战斗方案', { exact: true })).toBeVisible();
  const saved = await (await request.get(base + '/api/v1/profile')).json();
  expect(saved.profile.STRATEGY.find((entry: any) => entry.group_name === group.group_name).skill_settings.map((v: any) => v.role_var)).toEqual(expected);
  expect(saved.profile.STRATEGY.map((v: any) => `${v.group_name} ${v.skill_settings.length} 项`)).toEqual(expectedNames);
  await page.screenshot({ path: `test-results/combat-drag-${info.project.name}.png` });

  await page.getByRole('button', { name: '怪物配置', exact: true }).click();
  await page.getByRole('button', { name: '添加怪物', exact: true }).click();
  const dialog = page.getByRole('dialog', { name: '添加怪物头像' });
  await dialog.locator('input[type=file]').setInputFiles(resolve('../packs/wvd/image/combat_scorpion_portrait.png'));
  await expect(dialog.locator('img')).toBeVisible();
  await expect.poll(() => dialog.locator('footer span').innerText()).not.toBe('0 × 0');
  const monster = '头像验收-' + info.project.name + '-' + Date.now();
  await page.getByLabel('新怪物名称', { exact: true }).fill(monster);
  await page.getByLabel('新怪物战斗方案', { exact: true }).selectOption(group.group_name);
  await dialog.getByRole('button', { name: '确认添加', exact: true }).click();
  await expect(dialog).not.toBeVisible();
  await page.getByRole('button', { name: '保存常用参数', exact: true }).click();
  await expect(page.getByText('已保存常用参数', { exact: true })).toBeVisible();
  const withEnemy = await (await request.get(base + '/api/v1/profile')).json();
  const rule = withEnemy.profile.TASK_POINT_STRATEGY.special_combat.rules.find((v: any) => v.name === monster);
  expect(rule.strategy).toBe(group.group_name); expect(rule.portrait_image).toMatch(/^custom\/monster_[a-f0-9]{64}$/);
  expect(rule.portrait_png_base64.length).toBeGreaterThan(100);
  await page.reload();
  await page.getByRole('button', { name: '常用参数', exact: true }).click();
  await expect.poll(() => page.locator('.enemy-rule img').evaluateAll(nodes => nodes.every(node => (node as HTMLImageElement).complete && (node as HTMLImageElement).naturalWidth > 0))).toBe(true);
  const builtin = await request.get(base + '/api/v1/combat/portraits/' + Buffer.from('combat_scorpion_portrait').toString('hex'));
  expect(builtin.status()).toBe(200); expect(builtin.headers()['content-type']).toBe('image/png');
  await page.locator('.enemy-rules').scrollIntoViewIfNeeded();
  await page.screenshot({ path: `test-results/combat-enemies-${info.project.name}.png` });
  await page.getByRole('button', { name: '常用参数', exact: true }).click();
  await page.getByRole('option', { name: `${group.group_name} ${group.skill_settings.length} 项`, exact: true }).click();
  await page.getByRole('button', { name: '开始调试', exact: true }).click();
  await expect.poll(() => debug.length).toBe(1);
  expect(debug[0].strategy_name).toBe(group.group_name);
  expect(debug[0].profile_revision).toBe(withEnemy.revision);
  expect(forbidden).toEqual([]);
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
  await page.getByRole('button', { name: '常用参数', exact: true }).click();
  while (await page.getByRole('button', { name: '删除怪物规则', exact: true }).count())
    await page.getByRole('button', { name: '删除怪物规则', exact: true }).first().click();
  await expect(page.getByRole('checkbox', { name: '行动栏头像识别特殊敌人', exact: true })).not.toBeChecked();
  await page.getByRole('button', { name: '保存常用参数', exact: true }).click();
  await expect(page.getByText('已保存常用参数', { exact: true })).toBeVisible();
  const removed = await (await request.get(base + '/api/v1/profile')).json();
  expect(removed.profile.TASK_POINT_STRATEGY.special_combat.rules).toEqual([]);
  expect(removed.profile.TASK_POINT_STRATEGY.special_combat.portrait).toBe(false);
});
