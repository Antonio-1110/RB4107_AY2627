// Optional browser check run by smoke_test.py (RB4107_BROWSER_CHECK=1).
const assert = require('node:assert/strict');
const {chromium} = require('playwright');
const base = process.argv[2];
(async () => {
  const browser = await chromium.launch({headless:true,
    ...(process.env.RB4107_CHROMIUM_PATH ? {executablePath:process.env.RB4107_CHROMIUM_PATH} : {}),
    args:['--no-sandbox','--disable-dev-shm-usage']});
  try {
    const page = await browser.newPage({viewport:{width:1440,height:1000}}), errors=[];
    page.on('pageerror', error => errors.push(error.message));
    await page.goto(base);
    await page.waitForFunction(() => document.getElementById('summary-total').textContent === '9');
    assert.equal(await page.locator('.global-alert').count(), 1);
    assert.equal(await page.locator('.global-alert-warning').count(), 0);
    assert.match(await page.locator('.alert-title').innerText(), /MANUAL RESET REQUIRED/);
    await page.locator('.global-alert .alert-button').click();
    await page.waitForFunction(() => document.getElementById('chart-caption').textContent.includes('samples'));
    assert.equal(await page.locator('#sensor-rows tr').count(), 3);
    assert.match(await page.locator('#detail-location-line').innerText(), /Terminal 2/);
    assert.match(await page.locator('#reset-required').innerText(), /Yes/);
    assert.match(await page.locator('#thermal-extra').innerText(), /°C\/min/);
    await page.locator('#overview-tab').click();
    await page.locator('button.stall-link', {hasText:'Demo food court stall'}).click();
    assert.equal(await page.locator('#device-select option').count(), 2);
    await page.locator('#overview-tab').click();
    await page.locator('button.stall-link', {hasText:'Controller 01 — test bench'}).click();
    await page.waitForFunction(() => Number(document.getElementById('event-count').textContent) > 0);
    assert(await page.locator('#test-timers-badge').isVisible());
    await page.setViewportSize({width:390,height:844});
    assert.equal(await page.evaluate(() => document.documentElement.scrollWidth > innerWidth), false);
    await page.locator('#overview-tab').click();
    await page.setViewportSize({width:1440,height:1100});
    if (process.env.RB4107_SCREENSHOT_PATH) await page.screenshot({path:process.env.RB4107_SCREENSHOT_PATH,fullPage:true});

    // Render a warning-only fleet; removing the top banner must leave no gap.
    const data = await (await fetch(base+'/api/devices/')).json();
    data.alerts = data.alerts.filter(alert=>alert.severity==='warning');
    await page.route('**/api/devices/', route => route.fulfill({json:data}));
    await page.reload();
    await page.waitForFunction(() => document.getElementById('server-status').textContent === 'Server connected');
    assert.equal(await page.locator('.global-alert').count(), 0);
    assert.equal(await page.locator('#global-alerts').evaluate(el=>el.getBoundingClientRect().height), 0);
    assert((await page.locator('#stall-rows .state-badge.warning').count()) > 0);
    assert.deepEqual(errors, []);
    console.log('PASS: real browser overview, cutoff-only top banner, detail, three sensors, graph, event log, two-station stall and mobile layout');
  } finally { await browser.close(); }
})().catch(error=>{console.error(error);process.exitCode=1;});
