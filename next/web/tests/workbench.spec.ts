import { test, expect } from "@playwright/test";

// 原迁移盘点仅为历史审计，不再是产品路由。正式功能仍由 native-workbench 与 closure 用例保护。
test("产品入口退出迁移盘点与专属资产", async ({ page }) => {
  const requests: string[] = [];
  page.on("request", request => requests.push(request.url()));
  await page.goto("/");
  await expect(page.getByRole("button", { name: "工作台", exact: true })).toBeVisible();
  await expect(page.getByRole("button", { name: "流程编辑", exact: true })).toBeVisible();
  await expect(page.getByRole("button", { name: "迁移盘点", exact: true })).toHaveCount(0);
  await expect(page.getByRole("button", { name: "停止", exact: true })).toBeVisible();
  expect(requests.filter(url => /\/(migration|reference-assets)\//.test(url))).toEqual([]);
});
