#include <nano/node/insight/insight_server.hpp>

namespace
{
// Single-page dashboard. Polls /api/snapshot and re-renders. Vanilla JS, no build step.
char const * const index_html_data = R"INSIGHT_HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Nano Insight</title>
<style>
  :root { color-scheme: dark; }
  * { box-sizing: border-box; }
  body { margin: 0; font-family: -apple-system, Segoe UI, Roboto, sans-serif; font-size: 13px;
         background: #1b1d21; color: #d7dbe0; }
  header, nav, main { max-width: 1024px; margin-left: auto; margin-right: auto; }
  header { display: flex; align-items: center; gap: 16px; padding: 8px 14px;
           background: #25272c; border-bottom: 1px solid #34373d; }
  header h1 { font-size: 15px; margin: 0; font-weight: 600; }
  .status { margin-left: auto; display: flex; gap: 18px; font-variant-numeric: tabular-nums; }
  .status b { color: #fff; font-weight: 600; }
  .dot { width: 9px; height: 9px; border-radius: 50%; display: inline-block; margin-right: 6px;
         background: #e5484d; }
  .dot.live { background: #46a758; }
  nav { display: flex; gap: 2px; padding: 0 10px; background: #25272c; border-bottom: 1px solid #34373d; }
  nav button { background: none; border: none; color: #9aa0a8; padding: 9px 14px; cursor: pointer;
               font-size: 13px; border-bottom: 2px solid transparent; }
  nav button.active { color: #fff; border-bottom-color: #6e56cf; }
  main { padding: 10px 14px; }
  table { width: 100%; border-collapse: collapse; font-variant-numeric: tabular-nums; }
  table.narrow { width: auto; min-width: 380px; max-width: 520px; }
  th, td { text-align: left; padding: 5px 10px; border-bottom: 1px solid #2c2f35; white-space: nowrap; }
  th { color: #8b919a; font-weight: 600; }
  td.num, th.num { text-align: right; }
  tr:hover td { background: #232529; }
  .pr { background: #6e56cf; color: #fff; border-radius: 4px; padding: 1px 6px; font-size: 11px; font-weight: 600; }
  .mono { font-family: ui-monospace, Menlo, Consolas, monospace; }
  .muted { color: #767c85; }
</style>
</head>
<body>
<header>
  <h1>Nano Insight</h1>
  <span><span id="dot" class="dot"></span><span id="conn">connecting…</span></span>
  <div class="status">
    <span>msgs out/s <b id="msg_out">0</b></span>
    <span>in/s <b id="msg_in">0</b></span>
    <span>bps <b id="bps">0</b></span>
    <span>cps <b id="cps">0</b></span>
    <span>blocks <b id="blocks">0</b></span>
    <span>cemented <b id="cemented">0</b></span>
  </div>
</header>
<nav id="tabs"></nav>
<main id="content"></main>

<script>
const TABS = ["Peers", "Representatives", "Queues"];
let current = "Peers";
let latest = null;

const $ = id => document.getElementById(id);
const fmt = n => Number(n || 0).toLocaleString("en-US");
// raw (10^30 per Nano) -> whole Nano with thousands separators
function nano(raw) {
  try { return (BigInt(raw || "0") / (10n ** 30n)).toLocaleString("en-US"); }
  catch (e) { return "0"; }
}

function buildTabs() {
  const nav = $("tabs");
  nav.innerHTML = "";
  TABS.forEach(name => {
    const b = document.createElement("button");
    b.textContent = name;
    b.className = name === current ? "active" : "";
    b.onclick = () => { current = name; buildTabs(); render(); };
    nav.appendChild(b);
  });
}
function resetTabs() { $("tabs").innerHTML = ""; buildTabs(); }

function table(headers, rows, tableCls) {
  const cls = headers.map(h => h.num ? "num" : "");
  let html = `<table class="${tableCls || ""}"><thead><tr>`;
  headers.forEach((h, i) => html += `<th class="${cls[i]}">${h.label}</th>`);
  html += "</tr></thead><tbody>";
  rows.forEach(r => {
    html += "<tr>";
    r.forEach((c, i) => html += `<td class="${cls[i]} ${c.cls || ""}">${c.html}</td>`);
    html += "</tr>";
  });
  html += "</tbody></table>";
  return html;
}

function render() {
  if (!latest) return;
  const c = $("content");
  if (current === "Peers") {
    const rows = (latest.peers || []).map(p => [
      { html: p.endpoint, cls: "mono" },
      { html: p.principal === "true" || p.principal === true ? '<span class="pr">PR</span>' : "" },
      { html: nano(p.rep_weight), num: true },
      { html: p.block_count !== undefined ? fmt(p.block_count) : '<span class="muted">—</span>', num: true },
      { html: p.cemented_count !== undefined ? fmt(p.cemented_count) : '<span class="muted">—</span>', num: true },
      { html: p.unchecked_count !== undefined ? fmt(p.unchecked_count) : '<span class="muted">—</span>', num: true },
      { html: p.maker || '<span class="muted">—</span>' },
      { html: p.version ? "v" + p.version : '<span class="muted">—</span>' },
    ]);
    c.innerHTML = table(
      [{label:"Remote Addr"},{label:"Rep"},{label:"Rep Weight",num:1},{label:"Blocks",num:1},
       {label:"Cemented",num:1},{label:"Unchecked",num:1},{label:"Maker"},{label:"Version"}], rows);
  } else if (current === "Representatives") {
    const rows = (latest.representatives || []).map(r => [
      { html: r.account, cls: "mono" },
      { html: r.principal === "true" || r.principal === true ? '<span class="pr">PR</span>' : "" },
      { html: nano(r.weight), num: true },
    ]);
    c.innerHTML = table([{label:"Account"},{label:""},{label:"Weight (NANO)",num:1}], rows, "narrow");
  } else if (current === "Queues") {
    const q = latest.queues || {};
    const ae = q.active_elections || {};
    const rows = [
      ["Active elections — priority", ae.priority],
      ["Active elections — hinted", ae.hinted],
      ["Active elections — optimistic", ae.optimistic],
      ["Active elections — total", ae.total],
      ["Block processor", q.block_processor],
      ["Vote processor", q.vote_processor],
      ["Confirming set", q.confirming],
    ].map(([label, v]) => [{ html: label }, { html: fmt(v), num: true }]);
    c.innerHTML = table([{label:"Queue"},{label:"Size",num:1}], rows, "narrow");
  }
}

function applyStatus(s) {
  const r = s.rates || {}, l = s.ledger || {};
  $("msg_out").textContent = fmt(r.messages_out_per_second);
  $("msg_in").textContent = fmt(r.messages_in_per_second);
  $("bps").textContent = fmt(r.blocks_per_second);
  $("cps").textContent = fmt(r.confirmations_per_second);
  $("blocks").textContent = fmt(l.block_count);
  $("cemented").textContent = fmt(l.cemented_count);
}

async function poll() {
  try {
    const res = await fetch("/api/snapshot", { cache: "no-store" });
    latest = await res.json();
    $("dot").className = "dot live";
    $("conn").textContent = "live";
    applyStatus(latest);
    render();
  } catch (e) {
    $("dot").className = "dot";
    $("conn").textContent = "disconnected";
  }
}

resetTabs();
poll();
setInterval(poll, 1000);
</script>
</body>
</html>
)INSIGHT_HTML";
}

std::string_view nano::insight::index_html ()
{
	return index_html_data;
}
