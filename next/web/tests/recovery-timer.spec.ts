import { test, expect } from '@playwright/test';

test('恢复计时保留相同值新对象轮询、切换重置及卸载清理', async ({ page }, info) => {
  await page.goto('/tests/recovery-timer.html');
  await page.waitForFunction(() => Boolean((window as any).timerFixture));
  for (let i = 0; i < 4; i++) {
    await page.waitForTimeout(1500);
    await page.evaluate(() => (window as any).timerFixture.update(1, true));
  }
  await expect(page.locator('dl')).toContainText(/本页已观察 [5-9] 秒/);
  await page.screenshot({ path: info.outputPath('recovery-timer.png') });
  await page.evaluate(() => (window as any).timerFixture.update(2, true));
  await expect(page.locator('dl')).toContainText('本页已观察 0 秒');
  await page.waitForTimeout(1200);
  await expect(page.locator('dl')).toContainText('本页已观察 1 秒');
  await page.evaluate(() => (window as any).timerFixture.update(2, false));
  await expect(page.locator('dl')).not.toContainText('本页已观察');
  await page.evaluate(() => (window as any).timerFixture.update(2, true));
  await expect(page.locator('dl')).toContainText('本页已观察 0 秒');
  await page.evaluate(() => (window as any).timerFixture.unmount());
  expect(await page.evaluate(() => (window as any).timerFixture.intervals())).toBe(0);
});
