import { test, expect } from "@playwright/test";

test("退出工具保留取消后的草稿，确认后关闭真实后台", async ({ page, request }) => {
  const identity = await (await request.get("/api/v1/service")).json();
  // 本用例会退出服务，只接受明确隔离的目录，不能误操作正式后台。
  expect(String(identity.data_root)).toContain("service-lifecycle-ui-");
  const before = await (await request.get("/api/v1/profile")).json();
  await page.goto("/");
  const exit = page.getByRole("button", { name: "退出工具", exact: true });
  await expect(exit).toBeEnabled();
  const interval = page.getByRole("spinbutton", { name: "旅店间隔", exact: true });
  await interval.fill("2");
  await exit.click();
  const dialog = page.getByRole("dialog", { name: "有未保存更改" });
  await expect(dialog).toBeVisible();
  await dialog.getByRole("button", { name: "取消", exact: true }).click();
  await expect(dialog).not.toBeVisible();
  await expect(interval).toHaveValue("2");
  expect((await (await request.get("/api/v1/profile")).json()).revision).toBe(before.revision);
  await exit.click();
  await dialog.getByRole("button", { name: "退出并放弃更改", exact: true }).click();
  await expect(page.getByText("退出请求已接收", { exact: true })).toBeVisible();
  await expect(page.getByRole("button", { name: "开始任务", exact: true })).toHaveCount(0);
  await expect(exit).toBeDisabled();
  await page.screenshot({ path: "../.local/service-lifecycle-ui-20261001/exit-confirmed.png" });
  await expect.poll(async () => {
    try { return (await request.get("/api/v1/service", { timeout: 1000 })).ok(); }
    catch { return false; }
  }, { timeout: 10000 }).toBe(false);
});
