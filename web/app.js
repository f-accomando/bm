const e = React.createElement;

function App() {
  const getTabFromHash = () => {
    const hash = window.location.hash.replace("#", "");
    if (["start", "2d", "3d", "sounds"].includes(hash)) {
      return hash;
    }
    return "start";
  };

  const PRESETS = [
    { id: "kitchen.bm", name: "Chaos Kitchen", badge: "3D + Music", desc: "Full 3D cooking game with original music & sound effects" },
    { id: "demo.bm", name: "bm Native Demo", badge: "2D 60 FPS", desc: "256 bouncing sprites and full scrolling tile map" },
    { id: "stages3d.bm", name: "Stages 3D", badge: "3D Dungeon", desc: "Software rasterized 3D stages exploration" }
  ];

  const [activeTab, setActiveTab] = React.useState(getTabFromHash);
  const [launcherNotice, setLauncherNotice] = React.useState("");
  const [cart, setCart] = React.useState(null);
  const [cartCover, setCartCover] = React.useState("");
  const [isRunning, setIsRunning] = React.useState(false);
  const [selectedPreset, setSelectedPreset] = React.useState("kitchen.bm");
  const cartBytes = React.useRef(null);

  const parseAndSetCart = (buf, fileName) => {
    const result = window.bmCart ? window.bmCart.parse(buf, fileName) : { ok: true, fileName, size: buf.byteLength, sections: [] };
    result.fileName = fileName;
    cartBytes.current = new Uint8Array(buf);
    setCart(result);
    setCartCover("");
    if (result.ok && result.cover) {
      const c = document.createElement("canvas");
      c.width = result.cover.width;
      c.height = result.cover.height;
      c.getContext("2d").putImageData(new ImageData(new Uint8ClampedArray(result.cover.rgba), c.width, c.height), 0, 0);
      setCartCover(c.toDataURL());
    }
  };

  const loadPreset = (presetFile) => {
    setSelectedPreset(presetFile);
    setLauncherNotice("");
    fetch(presetFile)
      .then((res) => {
        if (!res.ok) throw new Error("Preset " + presetFile + " not found (" + res.status + ")");
        return res.arrayBuffer();
      })
      .then((buf) => {
        parseAndSetCart(buf, presetFile);
      })
      .catch((err) => {
        console.warn("Preset preload notice:", err.message);
      });
  };

  React.useEffect(() => {
    loadPreset("kitchen.bm");
  }, []);

  const handleCartFile = (ev) => {
    const file = ev.target.files && ev.target.files[0];
    if (!file) return;
    setSelectedPreset(null);
    setLauncherNotice("");
    file.arrayBuffer().then((buf) => {
      parseAndSetCart(buf, file.name);
    });
  };

  const changeTab = (tab) => {
    window.location.hash = tab;
    setActiveTab(tab);
  };

  const handleStart = () => {
    if (isRunning) {
      if (window.bmLauncher && typeof window.bmLauncher.stop === "function") {
        window.bmLauncher.stop();
      }
      return;
    }

    if (window.bmLauncher && typeof window.bmLauncher.start === "function") {
      setLauncherNotice("");
      setIsRunning(true);
      window.bmLauncher.onstop = () => {
        setIsRunning(false);
      };

      const failed = (err) => {
        setIsRunning(false);
        setLauncherNotice(String(err && err.message ? err.message : err));
      };

      const targetBytes = cartBytes.current;
      const targetName = cart ? cart.fileName : (selectedPreset || "kitchen.bm");
      const run = targetBytes && window.bmLauncher.startBytes
        ? window.bmLauncher.startBytes(targetBytes, targetName)
        : window.bmLauncher.start(targetName);
      if (run && typeof run.catch === "function") run.catch(failed);
    } else {
      setLauncherNotice("The bm player is not connected yet.");
    }
  };

  const playSoundPreview = (name) => {
    const AC = window.AudioContext || window.webkitAudioContext;
    if (!AC) return;
    const ctx = new AC();
    const t = ctx.currentTime;

    if (name.includes("Jump")) {
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = "square";
      osc.frequency.setValueAtTime(160, t);
      osc.frequency.exponentialRampToValueAtTime(600, t + 0.16);
      gain.gain.setValueAtTime(0.2, t);
      gain.gain.exponentialRampToValueAtTime(0.01, t + 0.16);
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start(t);
      osc.stop(t + 0.16);
    } else if (name.includes("Laser")) {
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = "sawtooth";
      osc.frequency.setValueAtTime(880, t);
      osc.frequency.exponentialRampToValueAtTime(110, t + 0.2);
      gain.gain.setValueAtTime(0.25, t);
      gain.gain.exponentialRampToValueAtTime(0.01, t + 0.2);
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start(t);
      osc.stop(t + 0.2);
    } else if (name.includes("Coin")) {
      const osc = ctx.createOscillator();
      const gain = ctx.createGain();
      osc.type = "sine";
      osc.frequency.setValueAtTime(987, t);
      osc.frequency.setValueAtTime(1318, t + 0.08);
      gain.gain.setValueAtTime(0.25, t);
      gain.gain.exponentialRampToValueAtTime(0.01, t + 0.35);
      osc.connect(gain);
      gain.connect(ctx.destination);
      osc.start(t);
      osc.stop(t + 0.35);
    } else if (name.includes("Explosion")) {
      const bSize = Math.floor(ctx.sampleRate * 0.35);
      const buf = ctx.createBuffer(1, bSize, ctx.sampleRate);
      const data = buf.getChannelData(0);
      for (let i = 0; i < bSize; i++) data[i] = (Math.random() * 2 - 1) * Math.exp(-i / (ctx.sampleRate * 0.08));
      const src = ctx.createBufferSource();
      const gain = ctx.createGain();
      src.buffer = buf;
      gain.gain.setValueAtTime(0.3, t);
      gain.gain.exponentialRampToValueAtTime(0.01, t + 0.35);
      src.connect(gain);
      gain.connect(ctx.destination);
      src.start(t);
    } else {
      const notes = [261.63, 329.63, 392.00, 523.25];
      notes.forEach((freq, i) => {
        const osc = ctx.createOscillator();
        const gain = ctx.createGain();
        osc.type = "triangle";
        osc.frequency.setValueAtTime(freq, t + i * 0.09);
        gain.gain.setValueAtTime(0, t);
        gain.gain.setValueAtTime(0.2, t + i * 0.09);
        gain.gain.exponentialRampToValueAtTime(0.01, t + i * 0.09 + 0.18);
        osc.connect(gain);
        gain.connect(ctx.destination);
        osc.start(t + i * 0.09);
        osc.stop(t + i * 0.09 + 0.2);
      });
    }
  };

  const renderCart = () => {
    if (!cart) return null;
    if (!cart.ok) return e("p", { className: "notice", id: "cart-error" }, cart.error);
    return e("div", { className: "cart-info", id: "cart-info" },
      cartCover ? e("img", { src: cartCover, alt: "Cartridge cover", width: 88, height: 88 }) : null,
      e("div", null,
        e("strong", null, cart.title || cart.fileName),
        e("p", null, (cart.author ? "by " + cart.author + " — " : "") + (cart.width ? cart.width + "×" + cart.height : "Native") + (cart.b16 ? " (.b16)" : "") + ", " + cart.size + " bytes"),
        cart.sections && cart.sections.length ? e("p", null, "Sections: " + cart.sections.map((x) => x.name).join(", ")) : null,
        cart.crcOk === false ? e("p", { className: "notice" }, "The CRC-32 does not match: the file may be damaged.") : null
      )
    );
  };

  const header = e("header", { key: "header" },
    e("div", { className: "brand" },
      e("h1", null, "bm", e("span", { className: "brand-badge" }, "Web Player")),
      e("p", { className: "subtitle" }, "Bare-metal fantasy console running in WebAssembly (60 FPS, WebAudio, Gamepad API).")
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

  const nav = e("nav", { key: "nav", className: "tab-bar" },
    e("button", {
      className: "tab-button " + (activeTab === "start" ? "active" : ""),
      onClick: () => changeTab("start")
    }, "Start Player"),
    e("button", {
      className: "tab-button " + (activeTab === "2d" ? "active" : ""),
      onClick: () => changeTab("2d")
    }, "2D Assets"),
    e("button", {
      className: "tab-button " + (activeTab === "3d" ? "active" : ""),
      onClick: () => changeTab("3d")
    }, "3D Assets"),
    e("button", {
      className: "tab-button " + (activeTab === "sounds" ? "active" : ""),
      onClick: () => changeTab("sounds")
    }, "Sounds")
  );

  const renderStart = () => {
    return e("div", { className: "tab-content", id: "tab-start" },
      e("div", { className: "bm-intro" },
        e("h2", null, "What is bm?"),
        e("p", null, "bm is a lightweight, bare-metal operating system and fantasy console engine for Raspberry Pi Zero and handheld consoles (RGB30). This WebAssembly player runs the console's full C runtime, Lua VM, and software 3D rasterizer directly in your browser with zero plugins."),
        e("div", { className: "bm-features" },
          e("div", { className: "feature-pill" },
            e("span", { className: "feature-title" }, "Native WebAssembly"),
            e("span", { className: "feature-desc" }, "Unchanged C runtime & Lua engine compiled with Emscripten.")
          ),
          e("div", { className: "feature-pill" },
            e("span", { className: "feature-title" }, "Low-Latency Audio"),
            e("span", { className: "feature-desc" }, "AudioWorklet ring buffer running at 48 kHz stereo.")
          ),
          e("div", { className: "feature-pill" },
            e("span", { className: "feature-title" }, "Persistent Saves"),
            e("span", { className: "feature-desc" }, "Saves automatically synchronized with IndexedDB.")
          )
        )
      ),
      e("div", { className: "canvas-wrapper" },
        e("div", { className: "canvas-container" },
          e("canvas", { id: "bm-canvas", width: 640, height: 360 }),
          isRunning ? null : e("div", { className: "canvas-overlay-text" }, "640 × 360 — Click 'Start bm' to Run")
        ),
        e("div", { className: "controls" },
          e("button", {
            className: "primary-btn " + (isRunning ? "running" : ""),
            onClick: handleStart
          }, isRunning ? "■ Stop bm (Esc)" : "▶ Start bm"),
          e("label", { className: "file-btn" }, "Open custom .bm file: ",
            e("input", { type: "file", accept: ".bm,.b16", onChange: handleCartFile })),
          launcherNotice ? e("p", { className: "notice" }, launcherNotice) : null
        ),
        e("div", { className: "preset-bar" },
          e("span", { className: "preset-title" }, "Preset Cartridges:"),
          PRESETS.map((p) =>
            e("button", {
              key: p.id,
              className: "preset-btn " + (selectedPreset === p.id ? "active" : ""),
              onClick: () => loadPreset(p.id)
            }, p.name + " (" + p.badge + ")")
          )
        ),
        renderCart(),
        e("div", { className: "keyboard-guide" },
          e("h3", null, "Controls Mapping (Keyboard & Gamepad)"),
          e("div", { className: "key-grid" },
            e("div", null, e("span", { className: "key-tag" }, "Arrows / D-Pad"), "Move (Left/Right/Up/Down)"),
            e("div", null, e("span", { className: "key-tag" }, "Space / J"), "Button A (Gamepad: A)"),
            e("div", null, e("span", { className: "key-tag" }, "X / K"), "Button B (Gamepad: B)"),
            e("div", null, e("span", { className: "key-tag" }, "U / I"), "Button X / Y"),
            e("div", null, e("span", { className: "key-tag" }, "Enter"), "Start"),
            e("div", null, e("span", { className: "key-tag" }, "Backspace / RShift"), "Select"),
            e("div", null, e("span", { className: "key-tag" }, "Q / E"), "L1 / R1 Bumpers"),
            e("div", null, e("span", { className: "key-tag" }, "Esc"), "Quit Cartridge / Menu")
          )
        )
      )
    );
  };

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
                  onClick: () => playSoundPreview(item.name)
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
