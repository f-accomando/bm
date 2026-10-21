const e = React.createElement;

function App() {
  const getTabFromHash = () => {
    const hash = window.location.hash.replace("#", "");
    if (["start", "2d", "3d", "sounds"].includes(hash)) {
      return hash;
    }
    return "start";
  };

  const [activeTab, setActiveTab] = React.useState(getTabFromHash);
  const [launcherNotice, setLauncherNotice] = React.useState("");

  React.useEffect(() => {
    const onHashChange = () => {
      setActiveTab(getTabFromHash());
    };
    window.addEventListener("hashchange", onHashChange);
    return () => window.removeEventListener("hashchange", onHashChange);
  }, []);

  const changeTab = (tab) => {
    window.location.hash = tab;
    setActiveTab(tab);
  };

  const handleStart = () => {
    if (window.bmLauncher && typeof window.bmLauncher.start === "function") {
      setLauncherNotice("");
      window.bmLauncher.start("demo.bm");
    } else {
      setLauncherNotice("The bm player is not connected yet.");
    }
  };

  // Tab Header
  const header = e("header", { key: "header" },
    e("h1", null, "bm Web Player"),
    e("p", { className: "subtitle" }, "A web interface for bm games, cartridges, and assets.")
  );

  // Nav
  const nav = e("nav", { key: "nav", className: "tab-bar" },
    e("button", {
      className: `tab-button ${activeTab === "start" ? "active" : ""}`,
      onClick: () => changeTab("start")
    }, "Start"),
    e("button", {
      className: `tab-button ${activeTab === "2d" ? "active" : ""}`,
      onClick: () => changeTab("2d")
    }, "2D Assets"),
    e("button", {
      className: `tab-button ${activeTab === "3d" ? "active" : ""}`,
      onClick: () => changeTab("3d")
    }, "3D Assets"),
    e("button", {
      className: `tab-button ${activeTab === "sounds" ? "active" : ""}`,
      onClick: () => changeTab("sounds")
    }, "Sounds")
  );

  // Tab 1: Start
  const renderStart = () => {
    return e("div", { className: "tab-content", id: "tab-start" },
      e("div", { className: "canvas-container" },
        e("canvas", { id: "bm-canvas", width: 640, height: 360 })
      ),
      e("div", { className: "controls" },
        e("button", { className: "primary-btn", onClick: handleStart }, "Start bm"),
        launcherNotice ? e("p", { className: "notice" }, launcherNotice) : null
      )
    );
  };

  // Tab 2: 2D Assets
  const render2D = () => {
    const items = Array.from({ length: 8 }, (_, i) => ({
      name: `2D Asset #${i + 1}`,
      license: "placeholder"
    }));

    return e("div", { className: "tab-content", id: "tab-2d" },
      e("div", { className: "grid" },
        items.map((item, idx) =>
          e("div", { className: "card", key: idx },
            e("div", { className: "card-img-placeholder" }),
            e("div", { className: "card-title" }, item.name),
            e("div", { className: "card-license" }, item.license)
          )
        )
      )
    );
  };

  // Tab 3: 3D Assets
  const render3D = () => {
    const items = Array.from({ length: 8 }, (_, i) => ({
      name: `3D Model #${i + 1}`,
      license: "placeholder"
    }));

    return e("div", { className: "tab-content", id: "tab-3d" },
      e("div", { className: "grid" },
        items.map((item, idx) =>
          e("div", { className: "card", key: idx },
            e("div", { className: "card-img-placeholder" }),
            e("div", { className: "card-title" }, item.name),
            e("div", { className: "card-license" }, item.license)
          )
        )
      )
    );
  };

  // Tab 4: Sounds
  const renderSounds = () => {
    const items = Array.from({ length: 8 }, (_, i) => ({
      name: `Sound Track #${i + 1}`,
      length: "0:02"
    }));

    return e("div", { className: "tab-content", id: "tab-sounds" },
      e("div", { className: "sound-list" },
        items.map((item, idx) =>
          e("div", { className: "sound-row", key: idx },
            e("div", { className: "sound-info" },
              e("span", { className: "sound-name" }, item.name),
              e("span", { className: "sound-len" }, item.length)
            ),
            e("button", { className: "play-btn", disabled: true }, "Play")
          )
        )
      )
    );
  };

  let content;
  if (activeTab === "2d") content = render2D();
  else if (activeTab === "3d") content = render3D();
  else if (activeTab === "sounds") content = renderSounds();
  else content = renderStart();

  return e("div", { className: "app" }, header, nav, e("main", null, content));
}

const root = ReactDOM.createRoot(document.getElementById("root"));
root.render(e(App));
