browser.runtime.onMessage.addListener(async (message, sender) => {
  if (message?.type !== "graphx-phase3-page-zoom" || sender.tab?.id === undefined) {
    return;
  }
  await browser.tabs.setZoom(sender.tab.id, message.zoom);
});
