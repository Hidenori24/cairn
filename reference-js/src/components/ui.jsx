// 共通の部品: アイコン、メニュー、ポップオーバー、モーダル、トースト、バッジ類
import React, { useEffect, useLayoutEffect, useRef, useState } from "react";
import { core, dismissToast, initials } from "../core/core.js";

const PATHS = {
  plus: "M12 5v14M5 12h14", minus: "M5 12h14", undo: "M9 14 4 9l5-5M4 9h10a6 6 0 0 1 0 12h-3", redo: "M15 14l5-5-5-5M20 9H10a6 6 0 0 0 0 12h3",
  search: "M11 19a8 8 0 1 1 0-16 8 8 0 0 1 0 16zM21 21l-4.3-4.3", down: "M6 9l6 6 6-6", right: "M9 6l6 6-6 6", up: "M18 15l-6-6-6 6",
  more: "M5 12h.01M12 12h.01M19 12h.01", x: "M18 6 6 18M6 6l12 12", check: "M20 6 9 17l-5-5",
  gantt: "M3 6h9M8 12h12M14 18h7", board: "M4 4h5v16H4zM15 4h5v10h-5z", list: "M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01",
  sliders: "M4 6h9M17 6h3M4 12h3M11 12h9M4 18h11M19 18h1M15 4v4M7 10v4M17 16v4", user: "M20 21v-2a4 4 0 0 0-4-4H8a4 4 0 0 0-4 4v2M12 11a4 4 0 1 0 0-8 4 4 0 0 0 0 8z",
  message: "M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z", trash: "M3 6h18M8 6V4h8v2M19 6l-1 14H6L5 6M10 11v6M14 11v6",
  indent: "M3 6h18M11 12h10M3 18h18M3 9l3 3-3 3", outdent: "M3 6h18M11 12h10M3 18h18M6 9l-3 3 3 3", arrowUp: "M12 19V5M5 12l7-7 7 7", arrowDown: "M12 5v14M19 12l-7 7-7-7",
  folder: "M3 7a2 2 0 0 1 2-2h4l2 2h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2z", download: "M12 3v12M7 10l5 5 5-5M5 21h14", upload: "M12 21V9M7 14l5-5 5 5M5 3h14",
  link: "M10 13a5 5 0 0 0 7 0l3-3a5 5 0 0 0-7-7l-1 1M14 11a5 5 0 0 0-7 0l-3 3a5 5 0 0 0 7 7l1-1", calendar: "M3 5h18v16H3zM3 10h18M8 3v4M16 3v4",
  copy: "M9 9h11v11H9zM5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1", help: "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM9.1 9a3 3 0 0 1 5.8 1c0 2-3 3-3 3M12 17h.01",
  fit: "M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7", target: "M12 22a10 10 0 1 0 0-20 10 10 0 0 0 0 20zM12 16a4 4 0 1 0 0-8 4 4 0 0 0 0 8z",
  filter: "M3 5h18l-7 8v6l-4 2v-8z", grip: "M9 5h.01M9 12h.01M9 19h.01M15 5h.01M15 12h.01M15 19h.01", cmd: "M9 6a3 3 0 1 0-3 3h12a3 3 0 1 0-3-3v12a3 3 0 1 0 3-3H6a3 3 0 1 0 3 3z",
  warn: "M12 3 2 21h20zM12 10v5M12 18h.01", pencil: "M4 20h4L19 9l-4-4L4 16zM13 7l4 4", flag: "M5 21V4M5 4h12l-2 4 2 4H5",
};
export function Icon({ n, size = 16, className, strokeWidth = 2 }) {
  return (
    <svg className={"ic" + (className ? " " + className : "")} width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth={strokeWidth} strokeLinecap="round" strokeLinejoin="round" aria-hidden="true">
      <path d={PATHS[n] || ""} />
    </svg>
  );
}

export function Btn({ icon, children, variant, size, title, onClick, disabled, active, className, ...rest }) {
  return (
    <button type="button" className={"btn" + (variant ? " " + variant : "") + (size ? " " + size : "") + (active ? " on" : "") + (className ? " " + className : "")} title={title} aria-label={rest["aria-label"] || (children ? undefined : title)} onClick={onClick} disabled={disabled} {...rest}>
      {icon && <Icon n={icon} size={size === "sm" ? 14 : 16} />}
      {children != null && children !== false && <span>{children}</span>}
    </button>
  );
}

const TYPES = [[/epic|エピック/i, "epic", "E"], [/story|ストーリー/i, "story", "S"], [/sub|サブ/i, "sub", "s"], [/bug|バグ/i, "bug", "B"], [/milestone|マイルストーン/i, "mst", "◆"], [/task|タスク/i, "task", "✓"]];
export function TypeIcon({ type }) {
  let c = "other", ch = "•";
  for (const [re, k, g] of TYPES) if (re.test(type)) { c = k; ch = g; break; }
  return <span className={"ti " + c} title={type}>{ch}</span>;
}
export function Avatar({ name, size }) {
  if (!name) return null;
  return <span className={"av" + (size === "lg" ? " lg" : "")} title={name}>{initials(name)}</span>;
}
const STATUS_CAT = (s) => Engine.statusCat(s);
export function StatusPill({ status, onClick }) {
  const cat = STATUS_CAT(status);
  const Tag = onClick ? "button" : "span";
  return <Tag type={onClick ? "button" : undefined} className={"stp " + cat} onClick={onClick}>{status || "To Do"}</Tag>;
}
export function FloatChip({ r, plain }) {
  const k = r.done ? "done" : r.float < 0 ? "late" : r.float <= 10 ? "tight" : "ok";
  const t = r.done ? "完了" : r.float < 0 ? "−" + Math.max(1, Math.round(Math.abs(r.float) / 5)) + "週" : r.float + "日";
  return <span className={(plain ? "fl " : "chip ") + k}>{t}</span>;
}

/* ---- 外側クリックで閉じる ---- */
export function useOutside(ref, onClose, active = true) {
  useEffect(() => {
    if (!active) return;
    const h = (e) => { if (ref.current && !ref.current.contains(e.target)) onClose(e); };
    const k = (e) => { if (e.key === "Escape") { e.stopPropagation(); onClose(e); } };
    document.addEventListener("mousedown", h, true); document.addEventListener("keydown", k, true);
    return () => { document.removeEventListener("mousedown", h, true); document.removeEventListener("keydown", k, true); };
  }, [ref, onClose, active]);
}

/* ---- ポップオーバー: anchor（要素 or {x,y}）の位置に出す ---- */
export function Popover({ anchor, onClose, children, align = "left", width, className }) {
  const ref = useRef(null);
  const [pos, setPos] = useState({ left: -9999, top: -9999 });
  useOutside(ref, (e) => { if (anchor && anchor.contains && e && anchor.contains(e.target)) return; onClose(e); });
  useLayoutEffect(() => {
    const el = ref.current; if (!el) return;
    const a = anchor && anchor.getBoundingClientRect ? anchor.getBoundingClientRect() : { left: anchor.x, right: anchor.x, top: anchor.y, bottom: anchor.y };
    const w = el.offsetWidth, h = el.offsetHeight;
    let left = align === "right" ? a.right - w : a.left, top = a.bottom + 4;
    if (left + w > innerWidth - 8) left = innerWidth - w - 8;
    if (left < 8) left = 8;
    if (top + h > innerHeight - 8) top = Math.max(8, a.top - h - 4);
    setPos({ left, top });
  }, [anchor, align]);
  return <div ref={ref} className={"pop" + (className ? " " + className : "")} style={{ left: pos.left, top: pos.top, width }} role="menu">{children}</div>;
}

/* ---- メニュー: items = [{label, icon, kbd, onClick, danger, disabled, sep, header, checked}] ---- */
export function Menu({ anchor, items, onClose, align, width }) {
  const [i, setI] = useState(-1);
  const list = items.filter((x) => x && !x.sep && !x.header);
  const onKey = (e) => {
    if (e.key === "ArrowDown") { e.preventDefault(); setI((v) => Math.min(list.length - 1, v + 1)); }
    else if (e.key === "ArrowUp") { e.preventDefault(); setI((v) => Math.max(0, v - 1)); }
    else if (e.key === "Enter" && list[i] && !list[i].disabled) { e.preventDefault(); onClose(); list[i].onClick && list[i].onClick(); }
  };
  useEffect(() => { document.addEventListener("keydown", onKey, true); return () => document.removeEventListener("keydown", onKey, true); });
  return (
    <Popover anchor={anchor} onClose={onClose} align={align} width={width} className="menu">
      {items.filter(Boolean).map((it, k) => it.sep ? <div key={k} className="msep" /> : it.header ? <div key={k} className="mhead">{it.header}</div> : (
        <button key={k} type="button" role="menuitem" disabled={it.disabled} className={"mi" + (it.danger ? " danger" : "") + (list[i] === it ? " hi" : "")}
          onMouseEnter={() => setI(list.indexOf(it))} onClick={() => { onClose(); it.onClick && it.onClick(); }}>
          <span className="mic">{it.icon ? <Icon n={it.icon} size={15} /> : it.checked ? <Icon n="check" size={15} /> : null}</span>
          <span className="mlb">{it.label}</span>{it.kbd && <span className="kbd">{it.kbd}</span>}
        </button>
      ))}
    </Popover>
  );
}

/* ---- ボタンからメニューを開く ---- */
export function MenuButton({ items, children, align, className, title, variant, icon, size, active }) {
  const [open, setOpen] = useState(false); const ref = useRef(null);
  return (
    <>
      <button type="button" ref={ref} className={"btn" + (variant ? " " + variant : "") + (size ? " " + size : "") + (className ? " " + className : "") + (open || active ? " on" : "")} title={title} aria-haspopup="menu" aria-expanded={open} onClick={() => setOpen((v) => !v)}>
        {icon && <Icon n={icon} size={size === "sm" ? 14 : 16} />}{children != null && <span>{children}</span>}
      </button>
      {open && <Menu anchor={ref.current} items={typeof items === "function" ? items() : items} onClose={() => setOpen(false)} align={align} />}
    </>
  );
}

/* ---- モーダル ---- */
export function Modal({ title, onClose, children, footer, width = 560, sticky }) {
  const ref = useRef(null);
  useEffect(() => {
    const k = (e) => { if (e.key === "Escape") { e.stopPropagation(); onClose(); } };
    document.addEventListener("keydown", k, true);
    const f = ref.current && ref.current.querySelector("[data-autofocus]"); if (f) f.focus();
    return () => document.removeEventListener("keydown", k, true);
  }, []);
  return (
    <div className="ov" onMouseDown={(e) => { if (e.target === e.currentTarget && !sticky) onClose(); }}>
      <div className="dlg" ref={ref} role="dialog" aria-modal="true" aria-label={title} style={{ width: "min(" + width + "px,100%)" }}>
        <div className="dlg-h"><h2>{title}</h2><button type="button" className="btn ghost icon-only" aria-label="閉じる" onClick={onClose}><Icon n="x" /></button></div>
        <div className="dlg-b">{children}</div>
        {footer && <div className="dlg-f">{footer}</div>}
      </div>
    </div>
  );
}

export function Toasts() {
  return (
    <div className="toasts" role="status" aria-live="polite">
      {core.toasts.map((t) => (
        <div key={t.id} className="toast">
          <span>{t.msg}</span>
          {t.action && <button type="button" className="toast-a" onClick={() => { dismissToast(t.id); t.action.fn(); }}>{t.action.label}</button>}
          <button type="button" className="toast-x" aria-label="閉じる" onClick={() => dismissToast(t.id)}><Icon n="x" size={14} /></button>
        </div>
      ))}
    </div>
  );
}

/* ---- 小さな入力部品 ---- */
export function Field({ label, children, full }) {
  return <div className={"fld" + (full ? " full" : "")}><label>{label}</label><div className="fc">{children}</div></div>;
}
export const Kbd = ({ children }) => <span className="kbd">{children}</span>;
