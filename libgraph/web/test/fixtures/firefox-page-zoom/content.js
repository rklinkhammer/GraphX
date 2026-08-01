window.addEventListener("graphx-phase3-page-zoom", async (event) => {
  await browser.runtime.sendMessage({
    type: "graphx-phase3-page-zoom",
    zoom: event.detail,
  });
  window.dispatchEvent(new CustomEvent("graphx-phase3-page-zoom-applied"));
});
