// Mock implementation of window.bmLauncher for testing
window.bmLauncher = {
  start: function(cartUrl) {
    console.log("[bmLauncher] start called with cartUrl:", cartUrl);
    alert("bmLauncher started with cartUrl: " + cartUrl);
  }
};
