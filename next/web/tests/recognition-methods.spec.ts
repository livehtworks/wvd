import { test, expect } from '@playwright/test';
import fs from 'node:fs';

// 专用临时数据目录的真实服务；禁止设备操作和运行任务，不向正式17654写数据。
const base = 'http://127.0.0.1:18758';
test('识别配方：真实目录、选择保存重载、上传实帧定位', async ({ page, request }, info) => {
  test.setTimeout(60000);
  expect(process.env.WVD_RECOGNITION_ISOLATED).toBe('1');
  const blocked: string[] = [];
  await page.route('**/*', async route => {
    const req = route.request(), url = new URL(req.url());
    const safe = url.origin === base && (!url.pathname.startsWith('/api/') ||
      req.method() === 'GET' || /^\/api\/v1\/workflows(?:\/[^/]+)?$/.test(url.pathname));
    if (!safe) { blocked.push(req.method() + ' ' + url.pathname); await route.abort(); }
    else await route.continue();
  });
  const catalog = await (await request.get(base + '/api/v1/catalog')).json();
  const resource = catalog.semantic_resources.find((r: any) => r.value === 'outskirts.fortress.zone10');
  expect(resource.methods['zh-Hant']).toEqual({ default: 'template', available: ['template', 'ocr'] });
  expect(resource.methods.en.available).toEqual(['template']);
  const id = 'recognition-methods-' + info.project.name + '-' + Date.now();
  const created = await request.post(base + '/api/v1/workflows', { data: {
    id, name: '识别方式验收', description: '', resource_locale: 'zh-Hant', entry_node_id: 'read',
    nodes: [{ id: 'read', type: 'editor', position: { x: 0, y: 0 }, data: { label: '识别目标', node_type: 'recognition',
      parameters: { condition: { mode: 'semantic', id: resource.value } } } },
    { id: 'done', type: 'editor', position: { x: 300, y: 0 }, data: { label: '完成', node_type: 'end', parameters: { outcome: 'success' } } }],
    edges: [{ id: 'next', source: 'read', target: 'done', sourceHandle: 'success', data: { kind: 'sequence', order: 0 } }],
    events: {}, checks: { phase: 'business', inherit: [] }, interface: { kind: 'block', category: '验收', parameters: [], handoffs: [] },
    layout: { viewport: { x: 0, y: 0, zoom: 1 } },
  } });
  expect(created.ok(), await created.text()).toBe(true);
  await page.goto(base);
  await page.getByRole('button', { name: '流程编辑', exact: true }).click();
  await page.getByLabel('流程', { exact: true }).selectOption(id);
  await page.locator('.vue-flow__node[data-id="read"]').click();
  await expect(page.getByLabel('素材识别方式')).toHaveValue('');
  await page.getByLabel('素材识别方式').selectOption('ocr');
  await page.getByRole('button', { name: '保存', exact: true }).click();
  await expect(page.getByText('流程已由服务端校验并保存')).toBeVisible();
  await page.reload();
  await page.getByRole('button', { name: '流程编辑', exact: true }).click();
  await page.getByLabel('流程', { exact: true }).selectOption(id);
  await page.locator('.vue-flow__node[data-id="read"]').click();
  await expect(page.getByLabel('素材识别方式')).toHaveValue('ocr');
  await page.getByLabel('素材识别方式').scrollIntoViewIfNeeded();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth + 1)).toBe(true);
  await page.screenshot({ path: info.outputPath('recognition-methods.png'), fullPage: true });
  const saved = await (await request.get(base + '/api/v1/workflows/' + id)).json();
  expect(saved.nodes[0].data.parameters.condition.method).toBe('ocr');
  const response = await request.post(base + '/api/v1/recognition/probe', { data: {
    image_base64: fs.readFileSync(process.env.WVD_RECOGNITION_FRAME!).toString('base64'),
    resource_locale: 'zh-Hant', recognition: saved.nodes[0].data.parameters.condition,
  } });
  expect(response.ok(), await response.text()).toBe(true);
  const result = await response.json();
  expect(result.outcome).toBe('Hit');
  expect(result.box[0]).toBeGreaterThan(400);
  expect(result.box[1]).toBeGreaterThan(850);
  expect(result.box[1] + result.box[3]).toBeLessThan(915);
  await info.attach('production-upload-probe', { body: JSON.stringify(result), contentType: 'application/json' });
  expect(blocked).toEqual([]);
});
