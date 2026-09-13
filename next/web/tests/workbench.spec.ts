import { test, expect } from "@playwright/test";
import type { Inventory } from "../src/api/types";

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
  await page.getByLabel("类型", { exact: true }).selectOption("function");
  await page
    .getByRole("textbox", { name: "搜索基线项" })
    .fill("Factory.StateCombatCheck");
  await expect(rows).toHaveCount(1);
  await rows.first().getByRole("button").click();
  await expect(page.locator("aside")).toContainText(
    "WvdBattleRecognizer::StateCombatCheck",
  );
  await expect(page.locator("aside")).toContainText("M3-VISION");
  await expect(page.locator("aside")).not.toContainText("WvdPauseRecognizer");
  await page.screenshot({
    path: info.outputPath("combat-ownership.png"),
    fullPage: true,
  });
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

test("刷新同步详情、筛选及有效页码", async ({ page }, info) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  const response = await page.request.get("/migration/feature_inventory.json");
  expect(response.ok()).toBe(true);
  const baseline: Inventory = await response.json();
  const original = baseline.items
    .filter(
      (item) =>
        item.kind === "function" && item.legacy_symbol.startsWith("Factory."),
    )
    .slice(0, 41);
  expect(original).toHaveLength(41);
  let items = structuredClone(original);
  let requests = 0;
  let holdNext = false;
  let release: (() => void) | undefined;
  // 仅控制清单这个外部数据源；版本、能力、页面和资产仍来自真实原生服务。
  await page.route("**/migration/feature_inventory.json", async (route) => {
    requests++;
    if (holdNext) {
      holdNext = false;
      await new Promise<void>((resolve) => {
        release = resolve;
      });
    }
    await route.fulfill({
      json: { ...baseline, items, counts: { function: items.length } },
    });
  });
  await page.goto("/");
  const kind = page.getByLabel("类型", { exact: true });
  const query = page.getByRole("textbox", { name: "搜索基线项" });
  const refresh = page.getByRole("button", { name: "刷新", exact: true });
  const pagination = page.locator("footer.pagination");
  const rows = page.locator("table tbody tr");
  await kind.selectOption("function");
  await query.fill("Factory.");
  await expect(rows).toHaveCount(40);
  await page.getByRole("button", { name: "下一页" }).click();
  await expect(pagination).toContainText("2 / 2");
  await rows.first().getByRole("button").click();
  items[40] = {
    ...items[40],
    new_owner: "native/games/wvd/fixture-refresh",
    new_entry: "RefreshFixture::updated",
  };
  await refresh.click();
  await expect(page.locator("aside")).toContainText("RefreshFixture::updated");
  await expect(rows).toContainText("native/games/wvd/fixture-refresh");
  await expect(pagination).toContainText("2 / 2");
  await expect(query).toHaveValue("Factory.");
  await expect(kind).toHaveValue("function");
  await page.screenshot({
    path: info.outputPath("refresh-updated-detail.png"),
    fullPage: true,
  });

  // 内容不变时不能丢失选择或有效页码。
  await refresh.click();
  await expect(refresh).toBeEnabled();
  await expect(page.locator("aside")).toContainText("RefreshFixture::updated");
  await expect(pagination).toContainText("2 / 2");
  expect(requests).toBe(3);

  // 请求等待期间切换选择，返回时应保留最近选择而非请求前的对象。
  await page.getByRole("button", { name: "上一页" }).click();
  await rows.first().getByRole("button").click();
  holdNext = true;
  await refresh.click();
  await expect.poll(() => Boolean(release)).toBe(true);
  await rows.nth(1).getByRole("button").click();
  items[1] = { ...items[1], new_entry: "RefreshFixture::latest_selection" };
  release!();
  await expect(page.locator("aside")).toContainText(
    "RefreshFixture::latest_selection",
  );
  await expect(page.locator("aside h2")).toHaveText(items[1].legacy_symbol);

  await page.getByRole("button", { name: "下一页" }).click();
  await rows.first().getByRole("button").click();
  items = [structuredClone(original[0])];
  await refresh.click();
  await expect(pagination).toContainText("1 / 1");
  await expect(rows).toHaveCount(1);
  await expect(rows).toContainText(original[0].legacy_symbol);
  await expect(page.locator("aside")).toHaveCount(0);
  await expect(query).toHaveValue("Factory.");
  await expect(kind).toHaveValue("function");
  await page.screenshot({
    path: info.outputPath("refresh-clamped-page.png"),
    fullPage: true,
  });

  await rows.first().getByRole("button").click();
  items = [
    structuredClone(
      baseline.items.find(
        (item) =>
          item.kind === "function" &&
          !item.legacy_symbol.startsWith("Factory."),
      )!,
    ),
  ];
  await refresh.click();
  await expect(pagination).toContainText("1 / 1");
  await expect(rows).toContainText("没有匹配项");
  await expect(page.locator("aside")).toHaveCount(0);
  await expect(query).toHaveValue("Factory.");
  await expect(kind).toHaveValue("function");
  await expect(refresh).toBeEnabled();
  expect(requests).toBe(6);
  expect(
    await page.evaluate(
      () => document.documentElement.scrollWidth <= innerWidth,
    ),
  ).toBe(true);
  expect(errors).toEqual([]);
  await page.screenshot({
    path: info.outputPath("refresh-empty-filter.png"),
    fullPage: true,
  });
});
