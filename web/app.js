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
  const [cart, setCart] = React.useState(null);
  const [cartCover, setCartCover] = React.useState("");

  // Stage 1 of the web player: read a .bm file in the page (web/bmcart.js). Nothing is sent anywhere.
  const handleCartFile = (ev) => {
    const file = ev.target.files && ev.target.files[0];
    if (!file) return;
    file.arrayBuffer().then((buf) => {
      const result = window.bmCart ? window.bmCart.parse(buf, file.name) : { ok: false, error: "The cartridge reader is missing." };
      result.fileName = file.name;
      setCart(result);
      setCartCover("");
      if (result.ok && result.cover) {
        const c = document.createElement("canvas");
        c.width = result.cover.width;
        c.height = result.cover.height;
        c.getContext("2d").putImageData(new ImageData(new Uint8ClampedArray(result.cover.rgba), c.width, c.height), 0, 0);
        setCartCover(c.toDataURL());
      }
    });
  };

  const renderCart = () => {
    if (!cart) return null;
    if (!cart.ok) return e("p", { className: "notice", id: "cart-error" }, cart.error);
    return e("div", { className: "cart-info", id: "cart-info" },
      cartCover ? e("img", { src: cartCover, alt: "Cartridge cover", width: 88, height: 88 }) : null,
      e("div", null,
        e("strong", null, cart.title || cart.fileName),
        e("p", null, (cart.author ? "by " + cart.author + " � " : "") + cart.width + "�" + cart.height + (cart.b16 ? " (.b16)" : "") + ", " + cart.size + " bytes"),
        e("p", null, "Sections: " + cart.sections.map((x) => x.name).join(", ")),
        cart.crcOk ? null : e("p", { className: "notice" }, "The CRC-32 does not match: the file may be damaged.")
      )
    );
  };

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

  // Header with title and Buy Me A Coffee sticker
  const header = e("header", { key: "header" },
    e("div", { className: "brand" },
      e("h1", null, "bm", e("span", { className: "brand-badge" }, "Console & Web")),
      e("p", { className: "subtitle" }, "A bare-metal fantasy console, modern creative suite & web platform.")
    ),
    e("a", {
      className: "bmc-btn",
      href: "https://buymeacoffee.com",
      target: "_blank",
      rel: "noopener noreferrer"
    },
      e("span", { className: "bmc-icon" }, "☕"),
      e("span", null, "Buy me a coffee")
    )
  );

  // Tab navigation bar
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
      // What is bm introduction box
      e("div", { className: "bm-intro" },
        e("h2", null, "What is bm?"),
        e("p", null, "bm is a lightweight, bare-metal operating system and game engine designed for Raspberry Pi Zero and handheld consoles (RGB30). It boots in 2 seconds directly into a creative workstation with zero Linux overhead."),
        e("div", { className: "bm-features" },
          e("div", { className: "feature-pill" },
            e("span", { className: "feature-title" }, "Integrated Suite"),
            e("span", { className: "feature-desc" }, "bm Pixel, bm Code, bm Studio, bm Mesh & bm Sound.")
          ),
          e("div", { className: "feature-pill" },
            e("span", { className: "feature-title" }, "Single Cartridge"),
            e("span", { className: "feature-desc" }, "Self-contained .bm file bundling Lua, sprites, meshes & audio.")
          ),
          e("div", { className: "feature-pill" },
            e("span", { className: "feature-title" }, "Hardware Driven"),
            e("span", { className: "feature-desc" }, "Rock-solid 60 fps with low-power ARM and GPU optimization.")
          )
        )
      ),
      // Canvas container
      e("div", { className: "canvas-wrapper" },
        e("div", { className: "canvas-container" },
          e("canvas", { id: "bm-canvas", width: 640, height: 360 }),
          e("div", { className: "canvas-overlay-text" }, "640 × 360 — 60 FPS Viewport")
        ),
        e("div", { className: "controls" },
          e("button", { className: "primary-btn", onClick: handleStart }, "Start bm"),
          launcherNotice ? e("p", { className: "notice" }, launcherNotice) : null,
          e("label", { className: "file-btn" }, "Open a .bm file ",
            e("input", { type: "file", accept: ".bm,.b16", onChange: handleCartFile })),
          renderCart()
        )
      )
    );
  };

  // Tab 2: 2D Assets (with bm Pixel / bm SDK recommendation)
  const render2D = () => {
    const items = [
      { name: "Hero Sprite Sheet", file: "assets/2d_1.png", format: "128x128 PNG", editor: "bm Pixel" },
      { name: "Dungeon Tileset", file: "assets/2d_2.png", format: "16x16 Tiles", editor: "bm SDK (Map)" },
      { name: "Coins & Collectibles", file: "assets/2d_3.png", format: "Icon Sheet", editor: "bm Pixel" },
      { name: "Monospace 8x12 Font", file: "assets/2d_4.png", format: "PSF1 / Bitmap", editor: "bm Code" },
      { name: "Mecha Enemy Sprites", file: "assets/2d_5.png", format: "4-frame Walk", editor: "bm Pixel" },
      { name: "Hearts & HUD Icons", file: "assets/2d_6.png", format: "GUI Elements", editor: "bm Pixel" },
      { name: "Smoke Particles", file: "assets/2d_7.png", format: "8-frame FX", editor: "bm Pixel" },
      { name: "Parallax Skyline", file: "assets/2d_8.png", format: "Layered 320x180", editor: "bm SDK (Map)" },
    ];

    return e("div", { className: "tab-content", id: "tab-2d" },
      e("div", { className: "category-header" },
        e("h2", null, "2D Sprites, Tilesets & Fonts"),
        e("p", null, "Recommended editors: ", e("span", { className: "editor-badge" }, "bm Pixel"), " for drawing & sprite frames, ", e("span", { className: "editor-badge" }, "bm SDK"), " for multi-layer map painting.")
      ),
      e("div", { className: "grid" },
        items.map((item, idx) =>
          e("div", { className: "card", key: idx },
            e("div", { className: "card-img-wrapper" },
              e("img", { src: item.file, alt: item.name, className: "card-img" })
            ),
            e("div", { className: "card-content" },
              e("div", { className: "card-title" }, item.name),
              e("div", { className: "card-meta" },
                e("span", null, item.format),
                e("span", { className: "card-target-editor" }, item.editor)
              )
            )
          )
        )
      )
    );
  };

  // Tab 3: 3D Assets (with bm Studio / bm Mesh recommendation)
  const render3D = () => {
    const items = [
      { name: "Low-Poly Knight", file: "assets/3d_1.png", format: "64 Tris (GLB)", editor: "bm Studio / Animator" },
      { name: "Modular Town House", file: "assets/3d_2.png", format: "Building Prop", editor: "bm Studio" },
      { name: "Pine Tree Model", file: "assets/3d_3.png", format: "Flora Low-Poly", editor: "bm Studio" },
      { name: "Dungeon Treasure Chest", file: "assets/3d_4.png", format: "Rigged Chest", editor: "bm Mesh" },
      { name: "Defense Turret", file: "assets/3d_5.png", format: "Mechanical Prop", editor: "bm Studio" },
      { name: "Stone Archway", file: "assets/3d_6.png", format: "Scenery Prop", editor: "bm Mesh" },
      { name: "Cave Spider Mob", file: "assets/3d_7.png", format: "Skeletal Rig", editor: "bm Animator" },
      { name: "Crystal Obelisk", file: "assets/3d_8.png", format: "Light Probe Mesh", editor: "bm Studio" },
    ];

    return e("div", { className: "tab-content", id: "tab-3d" },
      e("div", { className: "category-header" },
        e("h2", null, "3D Models, Meshes & Animations"),
        e("p", null, "Recommended editors: ", e("span", { className: "editor-badge" }, "bm Studio"), " for 3D block & tile modeling, ", e("span", { className: "editor-badge" }, "bm Mesh"), " for vertex editing, ", e("span", { className: "editor-badge" }, "bm Animator"), " for bones.")
      ),
      e("div", { className: "grid" },
        items.map((item, idx) =>
          e("div", { className: "card", key: idx },
            e("div", { className: "card-img-wrapper" },
              e("img", { src: item.file, alt: item.name, className: "card-img" })
            ),
            e("div", { className: "card-content" },
              e("div", { className: "card-title" }, item.name),
              e("div", { className: "card-meta" },
                e("span", null, item.format),
                e("span", { className: "card-target-editor" }, item.editor)
              )
            )
          )
        )
      )
    );
  };

  // Tab 4: Sounds (now in the same card grid layout as 2D/3D!)
  const renderSounds = () => {
    const items = [
      { name: "8-bit Jump SFX", file: "assets/snd_1.png", format: "0:01 Wave", editor: "bm Sound (Synth)" },
      { name: "Retro Laser Blast", file: "assets/snd_2.png", format: "0:02 Pulse", editor: "bm Sound (Synth)" },
      { name: "Coin Pickup Chime", file: "assets/snd_3.png", format: "0:01 Sine", editor: "bm Sound (Synth)" },
      { name: "Explosion Impact", file: "assets/snd_4.png", format: "0:02 Noise", editor: "bm Sound (Synth)" },
      { name: "Title Theme (RIFF)", file: "assets/snd_5.png", format: "0:16 MML Track", editor: "bm Sound (Tracker)" },
      { name: "Dungeon Ambient", file: "assets/snd_6.png", format: "0:24 Loop", editor: "bm Sound (Tracker)" },
      { name: "Game Over Fanfare", file: "assets/snd_7.png", format: "0:04 Chords", editor: "bm Sound (Synth)" },
      { name: "Stage Clear Jingle", file: "assets/snd_8.png", format: "0:05 Arpeggio", editor: "bm Sound (Synth)" },
    ];

    return e("div", { className: "tab-content", id: "tab-sounds" },
      e("div", { className: "category-header" },
        e("h2", null, "Audio Effects, Synth Instruments & RIFF Music"),
        e("p", null, "Recommended editors: ", e("span", { className: "editor-badge" }, "bm Sound"), " for synthesizer sound design, WAV previews & RIFF tracker composition.")
      ),
      e("div", { className: "grid" },
        items.map((item, idx) =>
          e("div", { className: "card", key: idx },
            e("div", { className: "card-img-wrapper" },
              e("img", { src: item.file, alt: item.name, className: "card-img" })
            ),
            e("div", { className: "card-content" },
              e("div", { className: "card-title" }, item.name),
              e("div", { className: "card-meta" },
                e("span", null, item.format),
                e("span", { className: "card-target-editor" }, item.editor)
              ),
              e("div", { className: "sound-action-row" },
                e("button", {
                  className: "sound-play-btn",
                  onClick: () => alert("Playing preview of " + item.name + " (synth sample)")
                }, "▶ Play Preview")
              )
            )
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
