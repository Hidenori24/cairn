// コマンドパレット（Ctrl+K）: 操作の実行と、課題への移動を1か所から。
import React, { useEffect, useMemo, useRef, useState } from "react";
import { core } from "../core/core.js";
import { ui, setUi, selectOnly } from "../core/state.js";
import { commands } from "../core/actions.js";
import { Icon, TypeIcon } from "../components/ui.jsx";

export function Palette() {
  const [q, setQ] = useState(""), [i, setI] = useState(0), inp = useRef(null), list = useRef(null);
  const close = () => setUi({ palette: false });
  const items = useMemo(() => {
    const toks = q.trim().toLowerCase().split(/\s+/).filter(Boolean);
    const all = commands().map((c) => ({ kind: "cmd", key: "c:" + c.group + c.title, group: c.group, title: c.title, kbd: c.kbd, run: c.run }));
    const iss = (core.T && !core.T.error) ? core.P.issues.map((o) => ({ kind: "iss", key: "i:" + o.id, group: "課題へ移動", title: core.T.name[o.id], id: o.id, did: core.T.did[o.id], type: o.type, run: () => { selectOnly(o.id, { scroll: true }); } })) : [];
    const hit = (x) => { const hay = (x.group + " " + x.title + " " + (x.did || "")).toLowerCase(); return toks.every((t) => hay.indexOf(t) >= 0); };
    if (!toks.length) return all.slice(0, 40);
    return all.filter(hit).concat(iss.filter(hit).slice(0, 8));
  }, [q]);
  useEffect(() => { inp.current && inp.current.focus(); }, []);
  useEffect(() => { setI(0); }, [q]);
  useEffect(() => { const el = list.current && list.current.querySelector(".on"); if (el) el.scrollIntoView({ block: "nearest" }); }, [i]);
  const run = (it) => { close(); setTimeout(() => it.run(), 0); };
  const onKey = (e) => {
    if (e.isComposing) return;
    if (e.key === "ArrowDown") { e.preventDefault(); setI((v) => Math.min(items.length - 1, v + 1)); }
    else if (e.key === "ArrowUp") { e.preventDefault(); setI((v) => Math.max(0, v - 1)); }
    else if (e.key === "Enter") { e.preventDefault(); if (items[i]) run(items[i]); }
    else if (e.key === "Escape") { e.preventDefault(); close(); }
  };
  let lastG = null;
  return (
    <div className="ov pal-ov" onMouseDown={(e) => { if (e.target === e.currentTarget) close(); }}>
      <div className="pal" role="dialog" aria-label="コマンドパレット">
        <div className="pal-in"><Icon n="search" size={16} /><input ref={inp} value={q} placeholder="コマンドや課題を検索…" aria-label="コマンドを検索" onChange={(e) => setQ(e.target.value)} onKeyDown={onKey} /><span className="kbd">Esc</span></div>
        <div className="pal-list" ref={list} role="listbox">
          {!items.length && <div className="pal-empty">見つかりません</div>}
          {items.map((it, k) => {
            const head = it.group !== lastG ? <div className="pal-g" key={"g" + k}>{it.group}</div> : null; lastG = it.group;
            return (
              <React.Fragment key={it.key}>{head}
                <button type="button" role="option" aria-selected={k === i} className={"pal-i" + (k === i ? " on" : "")} onMouseMove={() => setI(k)} onClick={() => run(it)}>
                  {it.kind === "iss" && <TypeIcon type={it.type} />}
                  {it.kind === "iss" && <span className="rid">{it.did}</span>}
                  <span className="pt">{it.title}</span>{it.kbd && <span className="kbd">{it.kbd}</span>}
                </button>
              </React.Fragment>
            );
          })}
        </div>
      </div>
    </div>
  );
}
