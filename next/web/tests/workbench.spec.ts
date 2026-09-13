import { test, expect } from "@playwright/test";

test("盘点搜索、真实资源预览、响应式布局及异常状态", async ({ page }, info) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await page.goto("/");
  await expect(
    page.getByRole("heading", { name: "完整功能基线" }),
  ).toBeVisible();
  await expect(page.getByLabel("基线统计")).toContainText("250");
  await expect(page.getByLabel("基线统计")).toContainText("58");
  await expect(page.getByRole("alert")).toHaveCount(0);
  await page.screenshot({
    path: info.outputPath("baseline.png"),
    fullPage: true,
  });
  await page.getByLabel("类型", { exact: true }).selectOption("config");
  await page.getByRole("textbox", { name: "搜索基线项" }).fill("EMU_PATH");
  const rows = page.locator("table tbody tr");
  await expect(rows).toHaveCount(1);
  await rows.first().getByRole("button").click();
  await expect(
    page.getByRole("heading", { name: "EMU_PATH", exact: true }),
  ).toBeVisible();
  await expect(page.locator("aside")).toContainText("LegacyConfigImporter");
  await page.getByRole("button", { name: "关闭详情" }).click();
  await page.getByRole("button", { name: "资源核对", exact: true }).click();
  await expect(page.locator("table tbody tr")).toHaveCount(8);
  await expect(page.getByRole("table")).toContainText("dungFlag.png");
  await page.screenshot({
    path: info.outputPath("case-report.png"),
    fullPage: true,
  });
  await page.getByRole("button", { name: "迁移清单", exact: true }).click();
  await page.getByLabel("类型", { exact: true }).selectOption("asset");
  await page
    .getByRole("textbox", { name: "搜索基线项" })
    .fill("resources/images/next.png");
  await expect(rows).toHaveCount(1);
  await rows.first().getByRole("button").click();
  const image = page.locator("aside img");
  await expect(image).toBeVisible();
  const pixels = await image.evaluate((element: HTMLImageElement) => {
    const canvas = document.createElement("canvas");
    canvas.width = element.naturalWidth;
    canvas.height = element.naturalHeight;
    const ctx = canvas.getContext("2d")!;
    ctx.drawImage(element, 0, 0);
    const data = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
    const colors = new Set<string>();
    for (let i = 0; i < data.length; i += 4)
      colors.add(`${data[i]},${data[i + 1]},${data[i + 2]}`);
    return { width: canvas.width, height: canvas.height, colors: colors.size };
  });
  expect(pixels.width).toBeGreaterThan(0);
  expect(pixels.height).toBeGreaterThan(0);
  expect(pixels.colors).toBeGreaterThan(10);
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  await page.screenshot({
    path: info.outputPath("asset-preview.png"),
    fullPage: true,
  });
  // 仅隔离错误提示这一外部依赖分支；前面所有正例均请求真实 C++ 服务。
  await page.route("**/api/v1/version", (route) =>
    route.abort("connectionrefused"),
  );
  await page.getByRole("button", { name: "刷新", exact: true }).click();
  await expect(page.getByRole("alert")).toContainText("连接失败");
  await page.unroute("**/api/v1/version");
  await page.getByRole("button", { name: "刷新", exact: true }).click();
  await expect(page.getByRole("alert")).toHaveCount(0);
  expect(errors).toEqual([]);
});
