import { test, expect } from "@playwright/test";

test("原生候选工作台保存配置并切换流程页", async ({ page }, info) => {
  await page.goto("/");
  await expect(page.getByRole("heading", { name: "工作台" })).toBeVisible();
  await expect(page.getByRole("button", { name: "开始任务" })).toBeVisible();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth + 1))
    .toBeTruthy();
  const vpn = page.getByLabel("重启后自动启动 Clash 并打开 VPN");
  await expect(vpn).toBeVisible();
  const initial = await vpn.isChecked();
  if (initial) await vpn.uncheck();
  else await vpn.check();
  await page.getByRole("button", { name: "保存配置" }).click();
  await expect(page.getByRole("button", { name: "保存配置" })).toBeDisabled();
  await page.reload();
  await expect(page.getByLabel("重启后自动启动 Clash 并打开 VPN"))
    .toHaveJSProperty("checked", !initial);
  await page.screenshot({ path: info.outputPath("native-workbench-home.png") });
  await page.getByRole("navigation", { name: "主导航" })
    .getByRole("button", { name: "流程编辑" }).click();
  await expect(page.getByRole("button", { name: "新建流程" })).toBeVisible();
  await page.getByRole("navigation", { name: "主导航" })
    .getByRole("button", { name: "迁移盘点" }).click();
  await expect(page.getByRole("heading", { name: "迁移盘点" })).toBeVisible();
  await page.screenshot({ path: info.outputPath("native-workbench.png"), fullPage: true });
});

test("流程编辑撤销、重做和保存重开使用同一正式入口", async ({ page }) => {
  await page.goto("/");
  await page.getByRole("navigation", { name: "主导航" })
    .getByRole("button", { name: "流程编辑" }).click();
  const description = page.getByLabel("流程说明");
  await expect(description).toBeVisible();
  const before = await description.inputValue();
  const changed = `${before} offline-undo-check`;
  await description.fill(changed);
  await page.getByRole("button", { name: "撤销" }).click();
  await expect(description).toHaveValue(before);
  await page.getByRole("button", { name: "重做" }).click();
  await expect(description).toHaveValue(changed);
  await page.getByRole("button", { name: "保存", exact: true }).click();
  await expect(page.getByText("流程已由服务端校验并保存")).toBeVisible();
  await page.reload();
  await page.getByRole("navigation", { name: "主导航" })
    .getByRole("button", { name: "流程编辑" }).click();
  await expect(page.getByLabel("流程说明")).toHaveValue(changed);
});
