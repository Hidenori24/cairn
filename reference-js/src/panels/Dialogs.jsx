// ダイアログ類（新規プロジェクト、取り込み、書き出し、設定、負荷、ショートカット一覧など）
import React, { useEffect, useRef, useState } from "react";
import { core, templates, templateItemsFrom, saveTemplate, deleteTemplate, applyTemplate, createProject, editProject, deleteProject, doImport, parseBackup, exportText, saveFile, readFile, setProject, setRes, addScenario, setUI, setPref, fmtD, num, todayIso, toast, dupProject, fsLink, fsGrant, fsUnlink, fsSync } from "../core/core.js";
import { ui, useAll, openModal, closeModal } from "../core/state.js";
import { copyText, pickFile } from "../core/actions.js";
import { Icon, Btn, Modal, Field } from "../components/ui.jsx";

const Opt = ({ checked, onChange, title, desc, name }) => (
  <label className="opt"><input type="radio" name={name} checked={checked} onChange={onChange} /><span><b>{title}</b><br /><span className="muted">{desc}</span></span></label>
);

function NewProject() {
  const [name, setName] = useState("新しいプロジェクト"), [key, setKey] = useState(""), [tpl, setTpl] = useState("empty");
  const ok = () => { closeModal(); createProject(tpl, name.trim(), key.trim()); };
  return (
    <Modal title="新しいプロジェクト" onClose={closeModal} footer={<><Btn onClick={closeModal}>キャンセル</Btn><Btn variant="pri" onClick={ok}>作成</Btn></>}>
      <div className="fgrid">
        <Field label="名前"><input className="inp" data-autofocus value={name} onChange={(e) => setName(e.target.value)} onKeyDown={(e) => { if (e.key === "Enter" && !e.isComposing) ok(); }} /></Field>
        <Field label="キー"><input className="inp" value={key} placeholder="例: DEV" onChange={(e) => setKey(e.target.value)} /></Field>
      </div>
      <div className="opts">
        <Opt name="tpl" checked={tpl === "empty"} onChange={() => setTpl("empty")} title="空のプロジェクト" desc="課題をひとつずつ追加して作ります。" />
        <Opt name="tpl" checked={tpl === "demo"} onChange={() => setTpl("demo")} title="階層デモ" desc="Epic から玄孫まで5階層の例（セキュアブート対応）。" />
        <Opt name="tpl" checked={tpl === "reg"} onChange={() => setTpl("reg")} title="法規対応サンプル" desc="EU機械規則・CRA・IEC 62443 のスケジュール例（40件）。" />
      </div>
    </Modal>
  );
}
function EditProject() {
  const [name, setName] = useState(core.P.name), [key, setKey] = useState(core.P.key);
  const ok = () => { closeModal(); editProject(name.trim(), key.trim()); };
  return (
    <Modal title="名前・キーの変更" onClose={closeModal} footer={<><Btn variant="danger" onClick={() => openModal("confirm", { title: "プロジェクトを削除", msg: "「" + core.P.name + "」を削除します。" + (core.fs.handle ? "フォルダのCSVは「_削除済み」フォルダに移します（そこから戻せます）。" : "このブラウザからは元に戻せません。必要なら先に書き出してください。"), okText: "削除する", danger: true, fn: deleteProject })}>このプロジェクトを削除</Btn><span style={{ flex: 1 }} /><Btn onClick={closeModal}>キャンセル</Btn><Btn variant="pri" onClick={ok}>保存</Btn></>}>
      <div className="fgrid">
        <Field label="名前"><input className="inp" data-autofocus value={name} onChange={(e) => setName(e.target.value)} onKeyDown={(e) => { if (e.key === "Enter" && !e.isComposing) ok(); }} /></Field>
        <Field label="キー"><input className="inp" value={key} onChange={(e) => setKey(e.target.value)} /></Field>
      </div>
      <p className="muted sm">キーは、これから追加する課題の番号（例: DEV-23）に使います。すでにある課題のキーは変わりません。</p>
      <div className="line"><Btn icon="copy" onClick={() => { closeModal(); dupProject(); }}>このプロジェクトを複製</Btn></div>
    </Modal>
  );
}
function Confirm({ title, msg, okText, danger, fn }) {
  return <Modal title={title} onClose={closeModal} width={460} footer={<><Btn onClick={closeModal}>キャンセル</Btn><Btn variant={danger ? "danger-solid" : "pri"} onClick={() => { closeModal(); fn(); }}>{okText || "OK"}</Btn></>}><p>{msg}</p></Modal>;
}

/* ---- 取り込み ---- */
export function runImport(text, name, mode) {
  if (!text.trim()) { toast("CSVの内容がありません"); return false; }
  const r = doImport(text, name, mode);
  if (r.json) {
    const b = parseBackup(text);
    if (b.error) { toast(b.error); return false; }
    if (!b.conflicts.length) { closeModal(); b.run(false); return true; }
    openModal("conflict", { names: b.conflicts.map((p) => p.name), run: b.run }); return true;
  }
  if (r.error) { toast(r.error); return false; }
  closeModal(); return true;
}
function Import({ prefill, fname }) {
  const [text, setText] = useState(prefill || ""), [name, setName] = useState((fname || "").replace(/\.(csv|tsv|txt|json)$/i, "") || "取り込んだCSV"), [mode, setMode] = useState("new");
  const choose = () => pickFile(async (f) => { setText(await readFile(f)); setName(f.name.replace(/\.(csv|tsv|txt|json)$/i, "")); });
  return (
    <Modal title="CSVを取り込む" onClose={closeModal} width={640} sticky footer={<><Btn onClick={closeModal}>キャンセル</Btn><Btn variant="pri" onClick={() => runImport(text, name.trim() || "取り込んだCSV", mode)}>取り込む</Btn></>}>
      <div className="line"><Btn icon="folder" onClick={choose}>ファイルを選ぶ</Btn><span className="muted sm">JIRAのCSV、このアプリの書き出し、バックアップ（JSON）に対応。ウィンドウへのドロップも可</span></div>
      <textarea className="inp mono" rows={9} data-autofocus placeholder="ここにCSVを貼り付けるか、ファイルを選んでください" aria-label="CSVの内容" value={text} onChange={(e) => setText(e.target.value)} />
      <div className="opts">
        <Opt name="imode" checked={mode === "new"} onChange={() => setMode("new")} title="新しいプロジェクトとして追加" desc="今のプロジェクトはそのまま残ります。" />
        <Opt name="imode" checked={mode === "replace"} onChange={() => setMode("replace")} title={"今のプロジェクト「" + core.P.name + "」を置き換え"} desc="元に戻す（Ctrl+Z）で戻せます。" />
      </div>
      {mode === "new" && <Field label="新規の名前"><input className="inp" value={name} onChange={(e) => setName(e.target.value)} /></Field>}
    </Modal>
  );
}
function Conflict({ names, run }) {
  return (
    <Modal title="同じプロジェクトがあります" onClose={closeModal} width={500} footer={<><Btn onClick={closeModal}>キャンセル</Btn><Btn variant="pri" onClick={() => { closeModal(); run(false); }}>別のプロジェクトとして追加</Btn><Btn variant="danger-solid" onClick={() => { closeModal(); run(true); }}>上書き</Btn></>}>
      <p>「{names.join("」「")}」は、すでにこのブラウザにあります。上書きすると、いまの内容がファイルの内容に置き換わります（元には戻せません）。</p>
    </Modal>
  );
}

/* ---- 書き出し ---- */
function Export({ backup }) {
  const [kind, setKind] = useState(backup ? "backup" : "jira");
  const text = exportText(kind);
  const fn = (backup ? "roadmap-backup" : core.P.name) + (kind === "jira" || kind === "app" ? ".csv" : ".json");
  const meta = backup ? "全プロジェクト " + projCount() + "件" : kind === "share" ? "このプロジェクトの共有用JSON（コメント・設定込み）" : core.P.issues.length + "件・" + text.split(/\r?\n/)[0].split(",").length + "列";
  const save = () => saveFile(fn, text, kind === "jira" || kind === "app" ? "text/csv" : "application/json").then((r) => { if (r === "saved") toast("保存を依頼しました"); else if (r === "failed") toast("保存できませんでした。コピーを使ってください"); });
  return (
    <Modal title={backup ? "全プロジェクトのバックアップ" : "「" + core.P.name + "」の書き出し"} onClose={closeModal} width={640}
      footer={<><Btn onClick={closeModal}>閉じる</Btn><Btn icon="copy" onClick={() => copyText(text)}>コピー</Btn><Btn variant="pri" icon="download" onClick={save}>ファイルに保存</Btn></>}>
      {!backup && (
        <div className="opts">
          <Opt name="emode" checked={kind === "jira"} onChange={() => setKind("jira")} title="JIRAに取り込む用" desc="Start / Due に計画（最早）の日付、親には配下の範囲を入れます。期限は Deadline 列に残ります。JIRAの標準の階層（Epic > タスク > サブタスク）に合わせ、深い階層はタスク直下のサブタスクにして、元の階層は説明の先頭に残します。マイルストーンは Task に Milestone ラベルを付けます。日付の形式は yyyy-MM-dd で取り込みます。" />
          <Opt name="emode" checked={kind === "app"} onChange={() => setKind("app")} title="このアプリ用CSV（入力した値のまま）" desc="Start / Due を書き換えません。" />
          <Opt name="emode" checked={kind === "share"} onChange={() => setKind("share")} title="共有用JSON（このプロジェクトだけ）" desc="コメントや設定も含みます。相手の「CSVを取り込む」で読み込めます。" />
        </div>
      )}
      <textarea className="inp mono" rows={10} readOnly aria-label="書き出す内容" value={text} onFocus={(e) => e.target.select()} />
      <div className="muted sm">{meta}</div>
    </Modal>
  );
}
const projCount = () => { try { return JSON.parse(exportText("backup")).projects.length; } catch (e) { return 0; } };

/* ---- 保存先フォルダ ---- */
function FolderDlg() {
  useAll();
  const f = core.fs, [busy, setBusy] = useState(false);
  const run = (fn) => async () => { setBusy(true); try { await fn(); } finally { setBusy(false); } };
  const status = !f.handle ? "未設定（ブラウザ内にだけ保存）" : f.state === "need" ? "許可が必要（自動保存が止まっています）" : f.state === "error" ? "エラー: " + f.msg : f.state === "saving" ? "保存中…" : "自動保存中" + (f.saved ? "（最終 " + new Date(f.saved).toLocaleTimeString("ja-JP", { hour: "2-digit", minute: "2-digit" }) + "）" : "");
  return (
    <Modal title="保存先フォルダ" onClose={closeModal} width={600} footer={<Btn variant="pri" onClick={closeModal}>閉じる</Btn>}>
      <p>編集するたびに、選んだフォルダへ <b>プロジェクト名.csv</b>（JIRA互換の原本）と <b>プロジェクト名.settings.json</b>（基準日・休業日・表示など、CSVに入らない設定）を自動で書き込みます。フォルダごと持ち運べば、別のPCでも同じ内容を開けます。</p>
      <div className="fgrid">
        <Field label="いまの保存先"><div><b>{f.handle ? f.name : "—"}</b><div className="muted sm">{status}</div></div></Field>
      </div>
      <div className="line">
        <Btn variant={f.handle ? "" : "pri"} icon="folder" disabled={busy} onClick={run(fsLink)}>{f.handle ? "別のフォルダにする" : "フォルダを選ぶ"}</Btn>
        {f.handle && f.state === "need" && <Btn variant="pri" disabled={busy} onClick={run(fsGrant)}>許可して再開</Btn>}
        {f.handle && f.state !== "need" && <Btn disabled={busy} onClick={run(() => fsSync({ manual: true }))}>フォルダの変更を読み込む</Btn>}
        {f.handle && <Btn variant="danger" disabled={busy} onClick={run(fsUnlink)}>連携を解除</Btn>}
      </div>
      <ul className="muted sm" style={{ lineHeight: 1.7 }}>
        <li>選ぶのは <code>data/projects</code> のような専用のフォルダがおすすめです（「ドキュメント」「デスクトップ」「ダウンロード」そのものはブラウザの制限で選べません）。</li>
        <li>フォルダに入っている <code>.csv</code> は、開くたびに読み込みます。Excelなどで直接編集した場合も、アプリに戻ると反映されます。</li>
        <li>上書きする前の内容は、そのセッションの最初の1回だけ「_バックアップ」フォルダに残します。削除したプロジェクトは「_削除済み」に移します。</li>
        <li>ブラウザを開き直すと、書き込みの許可をもう一度求められることがあります（上部の「許可して再開」を押します）。</li>
        <li>ブラウザ内の保存は予備として残ります。フォルダを選んでいない間の編集は、選んだときにまとめて書き出します。</li>
        <li>手を付けていないサンプルは、フォルダには書き出しません。</li>
      </ul>
    </Modal>
  );
}

/* ---- 設定 ---- */
function Settings() {
  useAll();
  const { P, T } = core, [hol, setHol] = useState(P.hol), [scn, setScn] = useState("");
  const cols = P.ui.cols || {};
  const colRow = (k, l) => <label className="chk" key={k}><input type="checkbox" checked={!!cols[k]} onChange={(e) => setUI({ cols: { ...cols, [k]: e.target.checked } })} /> {l}</label>;
  return (
    <Modal title="プロジェクト設定" onClose={closeModal} width={620} footer={<Btn variant="pri" onClick={closeModal}>閉じる</Btn>}>
      <section className="grp"><div className="grp-h"><h3>日程の計算</h3></div>
        <div className="fgrid">
          <Field label="基準日（ここから前に進める計画の起点）"><div className="line"><input className="inp mono" type="date" aria-label="基準日" value={P.base} onChange={(e) => e.target.value && setProject({ base: e.target.value }, true)} /><Btn size="sm" onClick={() => setProject({ base: todayIso() }, true)}>今日にする</Btn></div></Field>
          <Field label="シナリオ（期間の組み合わせ）">
            <div className="line">
              {T.scenarios.map((s) => <Btn key={s} size="sm" active={P.scn === s} onClick={() => setProject({ scn: s }, true)}>{s}</Btn>)}
              <input className="inp" style={{ width: 140 }} placeholder="新しいシナリオ名" aria-label="シナリオ名" value={scn} onChange={(e) => setScn(e.target.value)} onKeyDown={(e) => { if (e.key === "Enter" && !e.isComposing) { addScenario(scn); setScn(""); } }} />
              <Btn size="sm" onClick={() => { addScenario(scn); setScn(""); }}>追加</Btn>
            </div>
          </Field>
        </div>
        <label className="chk"><input type="checkbox" checked={P.res.on} onChange={(e) => setRes({ on: e.target.checked })} /> 担当者ごとに同時作業を制限する（工数の制約）</label>
        {P.res.on && <label className="line sm">同時に持てる件数 <input className="inp mono" type="number" min="1" max="9" step="1" style={{ width: 70 }} value={P.res.cap} onChange={(e) => setRes({ cap: Math.max(1, Math.min(9, parseInt(e.target.value, 10) || 1)) })} /></label>}
        <p className="muted sm">オフのときは、全員が全部を同時にこなせる前提の計画です。オンにすると、担当者が決まっている作業を、最遅開始が早い順に並べ替えて順番に割り当てます。</p>
        <details className="mini"><summary>休業日の設定</summary>
          <p className="muted sm">土日に加えて、ここに書いた日を休みとして営業日を数えます（1行に1日、2026-12-29 の形式）。</p>
          <textarea className="inp mono" rows={5} aria-label="休業日" value={hol} onChange={(e) => setHol(e.target.value)} />
          <div className="line"><Btn size="sm" onClick={() => { setProject({ hol }, true); toast("休業日を反映しました"); }}>反映</Btn><Btn size="sm" onClick={() => { const h = DEFAULT_HOLIDAYS.join("\n"); setHol(h); setProject({ hol: h }, true); }}>初期値に戻す</Btn></div>
        </details>
      </section>
      <section className="grp"><div className="grp-h"><h3>表示</h3></div>
        <div className="line"><span className="muted sm">ガントの列:</span>{colRow("asg", "担当")}{colRow("status", "状況")}{colRow("dur", "期間")}{colRow("float", "余裕")}</div>
        <div className="line"><span className="muted sm">密度:</span>
          {[["normal", "標準"], ["compact", "コンパクト"]].map(([k, l]) => <Btn key={k} size="sm" active={core.prefs.density === k} onClick={() => setPref("density", k)}>{l}</Btn>)}
          <span className="muted sm" style={{ marginLeft: 12 }}>テーマ:</span>
          {[["auto", "自動"], ["light", "ライト"], ["dark", "ダーク"]].map(([k, l]) => <Btn key={k} size="sm" active={core.prefs.theme === k} onClick={() => setPref("theme", k)}>{l}</Btn>)}
        </div>
      </section>
      <section className="grp"><div className="grp-h"><h3>データ</h3></div>
        <p className="muted sm">{core.fs.handle ? "プロジェクトは、作業フォルダ「" + core.fs.name + "」にCSVとして自動保存しています（ブラウザ内にも控えを持っています）。" : "プロジェクトはこのブラウザに保存されています。CSVのフォルダに自動で保存したいときは、右上の保存先から作業フォルダを選んでください。"}</p>
        <div className="line"><Btn icon="folder" onClick={() => openModal("folder")}>保存先フォルダ</Btn></div>
        <div className="line"><Btn icon="download" onClick={() => openModal("export", { backup: true })}>全プロジェクトをバックアップ</Btn></div>
        <details className="mini"><summary>CSVの列（JIRAのエクスポートで足りる列と、追加できる列）</summary>
          <ul className="cols">
            <li><b>課題</b>: <code>Issue Id</code> <code>Issue Type</code> <code>Summary</code> <code>Priority</code> <code>Status</code> <code>Assignee</code> <code>Labels</code> <code>Description</code></li>
            <li><b>階層</b>: <code>Parent</code>（親の Issue Id か Issue key。何階層でも可）、旧形式の <code>Epic Link</code> も読みます</li>
            <li><b>依存</b>: <code>Outward issue link (Blocks)</code> / <code>Inward issue link (Blocks)</code>。親課題に付けると配下の全作業に効きます</li>
            <li><b>期間</b>: <code>Duration (weeks)</code> &gt; <code>Original estimate</code> &gt; <code>Start date</code>〜<code>Due date</code>の営業日数。親課題の期間は配下から集計します</li>
            <li><b>期限（逆算の起点）</b>: <code>Deadline</code>。親に付けると配下すべてに効きます。列が無ければ、後続のない課題の <code>Due date</code></li>
            <li><b>シナリオ</b>: <code>Duration (weeks) @名前</code></li>
            <li><b>担当・コメント・着手</b>: <code>Assignee</code> / <code>Comment</code>（JIRAと同じ「日時;投稿者;本文」）/ <code>Earliest start</code></li>
            <li><b>進捗</b>: <code>Status</code>（Done/完了は <code>Resolved</code> に固定、In Progress/進行中は <code>Start date</code> から、<code>Remaining (weeks)</code> で残り）</li>
            <li>それ以外の列も、そのまま保持して書き出します</li>
          </ul>
        </details>
      </section>
    </Modal>
  );
}

/* ---- 負荷 ---- */
function Workload() {
  const { P, T, R, cal } = core, wl = Engine.workload(T, R, cal);
  return (
    <Modal title="担当者の負荷" onClose={closeModal} width={720} footer={<Btn variant="pri" onClick={closeModal}>閉じる</Btn>}>
      {!wl.length ? <p className="muted">担当者が設定された未完了の作業がありません。</p> : (
        <table className="ltbl">
          <thead><tr>{["担当者", "未完了", "合計（週）", "計画上の同時作業の最大", "最後の完了予定", "待たされる作業"].map((h) => <th key={h}>{h}</th>)}</tr></thead>
          <tbody>{wl.map((x) => (
            <tr key={x.name}><td>{x.name}</td><td className="mono">{x.n}件</td><td className="mono">{num(x.weeks)}</td><td className={"mono" + (x.peak > (P.res.on ? P.res.cap : 1) ? " late" : "")}>{x.peak}件</td><td className="mono">{fmtD(x.last)}</td><td className="mono">{x.wait ? x.wait + "件" : "—"}</td></tr>
          ))}</tbody>
        </table>
      )}
      <p className="muted sm">「計画上の同時作業の最大」は、制限なしで計画したときに、同じ担当者へ同時に重なる作業の最大件数です。2件以上なら、人手が足りない可能性があります。</p>
    </Modal>
  );
}

/* ---- 警告 ---- */
function Warnings() {
  const w = (core.T && core.T.warns || []).concat(core.P.loadWarns || []);
  return <Modal title="読み込み時の注意" onClose={closeModal} width={560} footer={<Btn variant="pri" onClick={closeModal}>閉じる</Btn>}>{w.length ? <ul className="warns">{w.map((x, i) => <li key={i}>{x}</li>)}</ul> : <p className="muted">注意はありません。</p>}</Modal>;
}

/* ---- ショートカット ---- */
const KEYS = [
  ["移動・選択", [["↑ ↓", "行を移動"], ["← →", "閉じる／開く（← は親へ）"], ["Shift+↑↓ / Shift+クリック", "範囲を選択"], ["Ctrl+クリック", "選択に追加／解除"], ["Esc", "選択を解除"]]],
  ["編集", [["ダブルクリック / F2 / Enter", "名前をその場で編集"], ["N", "同じ階層に行を追加"], ["Shift+N", "子を追加"], ["Tab / Shift+Tab", "階層を下げる／上げる"], ["Alt+↑ ↓", "同じ階層で上下へ"], ["Del", "削除（元に戻せます）"], ["Space", "詳細パネルを開く／閉じる"]]],
  ["編集中", [["Enter", "確定（追加直後の行は、続けて次の行を追加）"], ["Shift+Enter", "確定して子を追加"], ["Ctrl+Enter", "確定して同じ階層に追加"], ["Esc", "取り消し"]]],
  ["全体", [["Ctrl+K", "コマンドパレット"], ["Ctrl+Z / Ctrl+Shift+Z", "元に戻す／やり直す"], ["1 / 2 / 3 / 4", "ガント／ボード／リスト／ギャップ"], ["/", "検索"], ["?", "この一覧"]]],
];
function Help() {
  return (
    <Modal title="ショートカット" onClose={closeModal} width={620} footer={<Btn variant="pri" onClick={closeModal}>閉じる</Btn>}>
      <div className="keys">{KEYS.map(([g, rows]) => (<div key={g}><h4>{g}</h4>{rows.map(([k, d]) => <div key={k} className="krow"><span className="kbd">{k}</span><span>{d}</span></div>)}</div>))}</div>
    </Modal>
  );
}

/* ---------- テンプレート ---------- */
function TplPreview({ items, name }) {
  return (
    <ol className="tpl-pv">
      {items.map((it, i) => (
        <li key={i} style={{ paddingLeft: (it.lv || 0) * 18 }}>
          <span className="n">{i + 1}</span><span className="nm">{String(it.n).replace(/\{名前\}/g, name || "（追加先の名前）")}</span>
          {it.w != null && <span className="w mono">{num(it.w)}週</span>}
          {(it.after || []).length > 0 && <span className="af muted sm">← {(it.after || []).map((j) => j + 1).join(", ")}</span>}
        </li>
      ))}
    </ol>
  );
}
function TplApply({ ids }) {
  useAll();
  const list = templates(), [sel, setSel] = useState(list[0].id), t = list.find((x) => x.id === sel) || list[0];
  const T = core.T, first = ids[0], names = ids.map((id) => T.did[id] + " " + T.name[id]);
  const nBusy = ids.filter((id) => T.kids[id] && T.kids[id].length).length;
  const ok = () => { closeModal(); applyTemplate(ids, t); };
  return (
    <Modal title="テンプレートから子タスクを追加" width={720} onClose={closeModal}
      footer={<><span className="muted sm" style={{ flex: 1 }}>{nBusy ? "すでに子タスクがある課題 " + nBusy + "件には、末尾に追加します。" : "追加後に期間・担当・期限を直せます。"}</span><Btn onClick={closeModal}>キャンセル</Btn><Btn variant="pri" data-autofocus onClick={ok}>{ids.length}件に追加</Btn></>}>
      <div className="tpl-to"><span className="muted sm">追加先</span><b>{names.slice(0, 3).join(" ／ ")}{names.length > 3 ? " ほか" + (names.length - 3) + "件" : ""}</b></div>
      <div className="tpl-body">
        <div className="tpl-list" role="listbox" aria-label="テンプレート">
          {list.map((x) => (
            <div key={x.id} role="option" aria-selected={sel === x.id} tabIndex={0} className={"tpl-it" + (sel === x.id ? " on" : "")} onClick={() => setSel(x.id)} onKeyDown={(e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); setSel(x.id); } }}>
              <span><b>{x.name}</b><br /><span className="muted sm">{x.desc}</span></span>
              {!x.builtin && <button type="button" className="btn ghost icon-only xs" aria-label={"「" + x.name + "」を削除"} title="このテンプレートを削除" onClick={(e) => { e.stopPropagation(); openModal("confirm", { title: "テンプレートを削除", msg: "「" + x.name + "」を削除します。すでに追加した子タスクは変わりません。", okText: "削除する", danger: true, fn: () => { deleteTemplate(x.id); openModal("tplApply", { ids }); } }); }}><Icon n="trash" size={13} /></button>}
            </div>
          ))}
        </div>
        <div className="tpl-pvbox"><div className="muted sm">追加される子タスク（番号は、先行する項目の番号です）</div><TplPreview items={t.items} name={T.name[first]} /></div>
      </div>
    </Modal>
  );
}
function TplSave({ id }) {
  const T = core.T, [name, setName] = useState(T.name[id] + " の進め方"), [w, setW] = useState(true);
  const items = templateItemsFrom(id, w);
  const ok = () => { if (!name.trim()) return; closeModal(); saveTemplate(name.trim(), items); };
  return (
    <Modal title="子タスクをテンプレートとして保存" width={560} onClose={closeModal} footer={<><Btn onClick={closeModal}>キャンセル</Btn><Btn variant="pri" onClick={ok} disabled={!name.trim()}>保存</Btn></>}>
      <Field label="テンプレート名" full><input className="inp" data-autofocus value={name} onChange={(e) => setName(e.target.value)} onKeyDown={(e) => { if (e.key === "Enter" && !e.isComposing) ok(); }} /></Field>
      <label className="line sm"><input type="checkbox" checked={w} onChange={(e) => setW(e.target.checked)} /> 期間（週）も保存する</label>
      <div className="tpl-pvbox"><div className="muted sm">「{T.did[id]} {T.name[id]}」の子タスク {items.length}件（先行関係も含む）</div><TplPreview items={items} name="" /></div>
      <p className="muted sm">テンプレートはこのブラウザに保存され、「共有JSON」や「バックアップ」にも含まれます。担当者・日付は保存しません。</p>
    </Modal>
  );
}

export function ModalHost() {
  useAll();
  const m = ui.modal; if (!m) return null;
  const p = m.props || {};
  switch (m.type) {
    case "newProject": return <NewProject />;
    case "editProject": return <EditProject />;
    case "confirm": return <Confirm {...p} />;
    case "import": return <Import {...p} />;
    case "conflict": return <Conflict {...p} />;
    case "export": return <Export {...p} />;
    case "folder": return <FolderDlg />;
    case "settings": return <Settings />;
    case "workload": return <Workload />;
    case "warnings": return <Warnings />;
    case "help": return <Help />;
    case "tplApply": return <TplApply {...p} />;
    case "tplSave": return <TplSave {...p} />;
    default: return null;
  }
}
