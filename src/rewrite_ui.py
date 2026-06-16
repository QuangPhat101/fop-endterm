import sys
import re
import os

def rewrite():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    input_path = os.path.join(script_dir, 'index.html')
    with open(input_path, 'r', encoding='utf-8') as f:
        content = f.read()

    script_match = re.search(r'<script>(.*?)</script>', content, re.DOTALL)
    if not script_match:
        print("Could not find script block")
        return

    script_content = script_match.group(1)

    new_html = f"""<!doctype html>
<html lang="vi">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>HALO Security Console</title>
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
<link href="https://fonts.googleapis.com/css2?family=Outfit:wght@300;400;500;600;700;800&family=JetBrains+Mono:wght@400;500;600;700&display=swap" rel="stylesheet">
<style>
:root {{
  --bg: #050505;
  --bg-rgb: 5, 5, 5;
  --accent: #10b981;
  --accent-rgb: 16, 185, 129;
  --danger: #ef4444;
  --warning: #f59e0b;
  --info: #3b82f6;
  --text: #fdfdfd;
  --text-muted: #888888;
  --panel: rgba(255, 255, 255, 0.02);
  --panel-border: rgba(255, 255, 255, 0.05);
  --inner: rgba(10, 10, 10, 0.6);
  --r-outer: 2rem;
  --r-inner: calc(2rem - 6px);
  --bezier: cubic-bezier(0.32, 0.72, 0, 1);
  --transition: all 0.7s var(--bezier);
  --transition-fast: all 0.3s var(--bezier);
}}

* {{
  box-sizing: border-box;
  margin: 0;
  padding: 0;
}}

html {{
  scroll-behavior: smooth;
}}

body {{
  min-height: 100dvh;
  background: var(--bg);
  color: var(--text);
  font-family: 'Outfit', sans-serif;
  line-height: 1.6;
  -webkit-font-smoothing: antialiased;
  position: relative;
  overflow-x: hidden;
}}

/* Noise and ambient gradient */
body::before {{
  content: "";
  position: fixed;
  inset: 0;
  z-index: 50;
  pointer-events: none;
  background-image: url("data:image/svg+xml,%3Csvg viewBox='0 0 200 200' xmlns='http://www.w3.org/2000/svg'%3E%3Cfilter id='noiseFilter'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.65' numOctaves='3' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='100%25' height='100%25' filter='url(%23noiseFilter)' opacity='0.03'/%3E%3C/svg%3E");
}}

body::after {{
  content: "";
  position: fixed;
  top: -20vh;
  left: 20vw;
  width: 60vw;
  height: 60vh;
  border-radius: 50%;
  background: radial-gradient(circle, rgba(var(--accent-rgb), 0.04) 0%, transparent 70%);
  pointer-events: none;
  z-index: -1;
}}

.shell {{
  width: min(1400px, 100% - 48px);
  margin: 0 auto;
  padding: 40px 0 120px;
  display: flex;
  flex-direction: column;
  gap: 64px;
}}

/* Typography */
h1, h2, h3, h4 {{
  font-weight: 600;
  letter-spacing: -0.03em;
  line-height: 1.1;
}}

.mono {{
  font-family: 'JetBrains Mono', monospace;
}}

.eyebrow-text {{
  font-family: 'JetBrains Mono', monospace;
  font-size: 11px;
  text-transform: uppercase;
  letter-spacing: 0.1em;
  color: var(--accent);
  font-weight: 500;
}}

/* Floating Nav */
.floating-nav {{
  position: sticky;
  top: 24px;
  z-index: 40;
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin: 0 auto;
  width: 100%;
  max-width: 1400px;
  padding: 12px 24px;
  background: rgba(10, 10, 10, 0.7);
  backdrop-filter: blur(24px);
  border: 1px solid var(--panel-border);
  border-radius: 99px;
  box-shadow: 0 24px 48px rgba(0,0,0,0.4), inset 0 1px 0 rgba(255,255,255,0.05);
}}

.brand {{
  display: flex;
  align-items: center;
  gap: 16px;
}}

.mark {{
  width: 40px;
  height: 40px;
  border-radius: 50%;
  background: var(--text);
  color: var(--bg);
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 700;
  font-size: 20px;
}}

.nav-title {{
  font-size: 18px;
  font-weight: 600;
  letter-spacing: -0.02em;
}}

.connection-pill {{
  display: flex;
  align-items: center;
  gap: 10px;
  padding: 8px 16px;
  border-radius: 99px;
  background: rgba(255, 255, 255, 0.03);
  font-size: 13px;
  color: var(--text-muted);
  border: 1px solid var(--panel-border);
}}

.dot {{
  width: 8px;
  height: 8px;
  border-radius: 50%;
  background: var(--danger);
  box-shadow: 0 0 12px var(--danger);
  transition: var(--transition-fast);
}}

.dot.ok {{
  background: var(--accent);
  box-shadow: 0 0 12px var(--accent);
}}

/* The Double-Bezel Card Architecture */
.card-shell {{
  background: var(--panel);
  border: 1px solid var(--panel-border);
  padding: 8px;
  border-radius: var(--r-outer);
  transition: var(--transition);
}}

.card-core {{
  background: var(--inner);
  border-radius: var(--r-inner);
  padding: 40px;
  box-shadow: inset 0 1px 1px rgba(255, 255, 255, 0.05);
  backdrop-filter: blur(24px);
  height: 100%;
}}

/* Form Elements */
input, select {{
  width: 100%;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid var(--panel-border);
  color: var(--text);
  padding: 16px 20px;
  border-radius: 16px;
  font-family: inherit;
  font-size: 15px;
  transition: var(--transition-fast);
  outline: none;
}}

select option {{
  background: #121212;
  color: var(--text);
}}

input:focus, select:focus {{
  background: rgba(255, 255, 255, 0.05);
  border-color: rgba(var(--accent-rgb), 0.4);
  box-shadow: 0 0 0 4px rgba(var(--accent-rgb), 0.1);
}}

input[type="datetime-local"]::-webkit-calendar-picker-indicator {{
  filter: invert(1);
  opacity: 0.5;
  cursor: pointer;
  transition: var(--transition-fast);
}}

input[type="datetime-local"]::-webkit-calendar-picker-indicator:hover {{
  opacity: 1;
}}

label {{
  display: flex;
  flex-direction: column;
  gap: 10px;
  font-size: 14px;
  font-weight: 500;
  color: var(--text-muted);
}}

.check-label {{
  flex-direction: row;
  align-items: center;
  cursor: pointer;
  padding: 16px;
  border: 1px solid var(--panel-border);
  border-radius: 16px;
  background: rgba(255, 255, 255, 0.02);
  transition: var(--transition-fast);
}}

.check-label:hover {{
  background: rgba(255, 255, 255, 0.04);
}}

.check-label input[type="checkbox"] {{
  width: 20px;
  height: 20px;
  accent-color: var(--accent);
  padding: 0;
}}

/* Buttons */
button {{
  cursor: pointer;
  border: none;
  font-family: inherit;
  transition: var(--transition-fast);
}}

.btn-primary {{
  display: inline-flex;
  align-items: center;
  justify-content: center;
  gap: 12px;
  background: var(--text);
  color: var(--bg);
  padding: 16px 32px;
  border-radius: 99px;
  font-weight: 600;
  font-size: 15px;
  border: 1px solid transparent;
}}

.btn-primary:hover {{
  transform: scale(0.98);
  background: #e0e0e0;
}}

.btn-primary:active {{
  transform: scale(0.95);
}}

.btn-primary:disabled {{
  opacity: 0.5;
  cursor: not-allowed;
  transform: none;
}}

.btn-primary .btn-icon-wrapper {{
  display: flex;
  align-items: center;
  justify-content: center;
  width: 28px;
  height: 28px;
  border-radius: 50%;
  background: rgba(0, 0, 0, 0.1);
  transition: var(--transition-fast);
}}

.btn-primary:hover .btn-icon-wrapper {{
  transform: translateX(2px) scale(1.05);
}}

/* Layout: Asymmetrical Bento */
.bento-grid {{
  display: grid;
  grid-template-columns: repeat(12, 1fr);
  gap: 24px;
}}

.col-span-full {{ grid-column: 1 / -1; }}
.col-span-4 {{ grid-column: span 4; }}
.col-span-8 {{ grid-column: span 8; }}
.col-span-6 {{ grid-column: span 6; }}
.col-span-3 {{ grid-column: span 3; }}

@media (max-width: 1024px) {{
  .col-span-4, .col-span-8, .col-span-6, .col-span-3 {{
    grid-column: 1 / -1;
  }}
}}

/* Load Rail */
.load-rail {{
  height: 4px;
  background: rgba(255, 255, 255, 0.05);
  border-radius: 99px;
  overflow: hidden;
  margin-top: 24px;
  display: none;
}}

.load-rail.active {{ display: block; }}

.load-rail-inner {{
  height: 100%;
  width: 30%;
  background: var(--accent);
  border-radius: 99px;
  animation: load 1.5s var(--bezier) infinite;
}}

@keyframes load {{
  0% {{ transform: translateX(-150%); }}
  100% {{ transform: translateX(350%); }}
}}

/* Monitor & Stats */
.monitor-header {{
  display: flex;
  justify-content: space-between;
  align-items: center;
  margin-bottom: 32px;
}}

.status-badge {{
  display: inline-flex;
  align-items: center;
  gap: 8px;
  padding: 6px 16px;
  background: rgba(var(--accent-rgb), 0.1);
  border: 1px solid rgba(var(--accent-rgb), 0.2);
  color: var(--accent);
  border-radius: 99px;
  font-size: 13px;
  font-weight: 500;
}}

.pulse {{
  width: 6px;
  height: 6px;
  border-radius: 50%;
  background: var(--accent);
  animation: pulse-anim 2s var(--bezier) infinite;
}}

@keyframes pulse-anim {{
  0% {{ transform: scale(1); opacity: 1; }}
  50% {{ transform: scale(1.5); opacity: 0.5; }}
  100% {{ transform: scale(1); opacity: 1; }}
}}

.pill-list {{
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  margin-bottom: 32px;
}}

.pill {{
  padding: 10px 20px;
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid var(--panel-border);
  border-radius: 99px;
  font-size: 14px;
  color: var(--text-muted);
  transition: var(--transition-fast);
}}

.pill:hover {{
  background: rgba(255, 255, 255, 0.05);
  color: var(--text);
}}

.pill.active {{
  background: rgba(var(--accent-rgb), 0.1);
  border-color: rgba(var(--accent-rgb), 0.3);
  color: var(--accent);
}}

.metric-group {{
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
  gap: 16px;
}}

.metric {{
  display: flex;
  flex-direction: column;
  gap: 8px;
}}

.metric-label {{
  font-size: 13px;
  color: var(--text-muted);
}}

.metric-value {{
  font-size: 24px;
  font-weight: 500;
  letter-spacing: -0.02em;
}}

/* Stat grid */
.stat-grid {{
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
  gap: 16px;
}}

.stat-box {{
  padding: 24px;
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid var(--panel-border);
  border-radius: 24px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}}

.stat-box span {{ color: var(--text-muted); font-size: 13px; }}
.stat-box strong {{ font-size: 28px; font-weight: 400; }}

/* Tabs */
.tabs-container {{
  display: flex;
  gap: 12px;
  margin-bottom: 40px;
}}

.tab-btn {{
  padding: 16px 32px;
  background: transparent;
  color: var(--text-muted);
  font-size: 16px;
  font-weight: 500;
  border-radius: 99px;
  border: 1px solid transparent;
}}

.tab-btn:hover {{
  color: var(--text);
}}

.tab-btn.active {{
  background: rgba(255, 255, 255, 0.03);
  color: var(--text);
  border-color: var(--panel-border);
}}

.tab-pane {{
  display: none;
  animation: fadeUp 0.6s var(--bezier) forwards;
  opacity: 0;
  transform: translateY(20px);
}}

.tab-pane.active {{
  display: block;
}}

@keyframes fadeUp {{
  to {{ opacity: 1; transform: translateY(0); }}
}}

/* Query UI */
.query-split {{
  display: grid;
  grid-template-columns: 320px 1fr;
  gap: 32px;
}}

@media (max-width: 1024px) {{
  .query-split {{ grid-template-columns: 1fr; }}
}}

.query-nav {{
  display: flex;
  flex-direction: column;
  gap: 12px;
}}

.query-nav-btn {{
  text-align: left;
  padding: 24px;
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid var(--panel-border);
  border-radius: 24px;
  display: flex;
  flex-direction: column;
  gap: 8px;
}}

.query-nav-btn:hover {{
  background: rgba(255, 255, 255, 0.04);
}}

.query-nav-btn.active {{
  background: rgba(255, 255, 255, 0.06);
  border-color: rgba(255, 255, 255, 0.15);
}}

.query-nav-btn strong {{ font-size: 16px; color: var(--text); }}
.query-nav-btn span {{ font-size: 13px; color: var(--text-muted); line-height: 1.4; }}

.query-form-pane {{ display: none; }}
.query-form-pane.active {{ display: block; animation: fadeUp 0.4s var(--bezier) forwards; }}

.time-presets {{
  display: flex;
  gap: 8px;
  margin-top: 12px;
}}

.preset-btn {{
  padding: 6px 14px;
  background: rgba(255, 255, 255, 0.03);
  border: 1px solid var(--panel-border);
  border-radius: 99px;
  font-size: 12px;
  color: var(--text-muted);
}}
.preset-btn:hover {{
  background: rgba(255, 255, 255, 0.08);
  color: var(--text);
}}

/* Anomaly UI */
.anomaly-split {{
  display: grid;
  grid-template-columns: 1fr 380px;
  gap: 32px;
}}
@media (max-width: 1024px) {{
  .anomaly-split {{ grid-template-columns: 1fr; }}
}}

.anomaly-desc {{
  margin: 32px 0;
  padding: 24px;
  background: rgba(var(--accent-rgb), 0.05);
  border-left: 2px solid var(--accent);
  border-radius: 0 16px 16px 0;
  color: var(--text-muted);
  font-size: 15px;
  line-height: 1.6;
}}

.param-grid {{
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 24px;
  margin-bottom: 32px;
}}

@media (max-width: 768px) {{
  .param-grid {{ grid-template-columns: 1fr; }}
}}

.radio-group {{
  display: flex;
  gap: 16px;
  margin-top: 16px;
}}

.radio-label {{
  flex: 1;
  display: flex;
  align-items: center;
  gap: 12px;
  padding: 16px;
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid var(--panel-border);
  border-radius: 16px;
  cursor: pointer;
  font-size: 14px;
  transition: var(--transition-fast);
}}

.radio-label:hover {{ background: rgba(255, 255, 255, 0.04); }}
.radio-label input {{ width: 18px; height: 18px; accent-color: var(--accent); padding: 0; }}

/* History */
.history-item {{
  padding: 20px;
  background: rgba(255, 255, 255, 0.02);
  border: 1px solid var(--panel-border);
  border-radius: 16px;
  margin-bottom: 12px;
  text-align: left;
  display: flex;
  flex-direction: column;
  gap: 8px;
  cursor: pointer;
}}

.history-item:hover {{
  background: rgba(255, 255, 255, 0.04);
}}
.history-item.active {{
  border-color: var(--accent);
  background: rgba(var(--accent-rgb), 0.05);
}}

.history-item-title {{
  display: flex;
  justify-content: space-between;
  align-items: center;
  font-weight: 500;
  font-size: 14px;
}}

.history-item-badge {{
  font-size: 10px;
  padding: 4px 8px;
  background: rgba(255, 255, 255, 0.05);
  border-radius: 99px;
  font-family: 'JetBrains Mono', monospace;
}}

.history-item-date {{ font-size: 12px; color: var(--text-muted); }}

.history-item-meta {{
  font-size: 11px;
  color: var(--text-muted);
  display: flex;
  justify-content: space-between;
  gap: 8px;
}}

.history-item-actions {{
  display: flex;
  gap: 8px;
  margin-top: 4px;
}}

.history-delete-btn {{
  font-size: 11px;
  color: var(--danger);
  padding: 4px 8px;
  border-radius: 6px;
  border: 1px solid rgba(239, 68, 68, 0.2);
  background: rgba(239, 68, 68, 0.06);
  transition: var(--transition-fast);
  cursor: pointer;
}}

.history-delete-btn:hover {{
  background: rgba(239, 68, 68, 0.14);
}}

.history-empty {{
  display: grid;
  place-items: center;
  height: 150px;
  color: var(--text-muted);
  font-size: 13px;
  font-style: italic;
  text-align: center;
  padding: 20px;
}}

/* Results Table */
.table-wrapper {{
  width: 100%;
  overflow-x: auto;
  margin-top: 32px;
}}

table {{
  width: 100%;
  border-collapse: collapse;
  font-size: 14px;
}}

th, td {{
  padding: 16px 24px;
  text-align: left;
  border-bottom: 1px solid var(--panel-border);
}}

th {{
  color: var(--text-muted);
  font-weight: 500;
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 0.05em;
}}

tbody tr:hover {{
  background: rgba(255, 255, 255, 0.01);
}}

/* Toast */
.toast {{
  position: fixed;
  bottom: 32px;
  right: 32px;
  background: var(--text);
  color: var(--bg);
  padding: 16px 24px;
  border-radius: 16px;
  font-weight: 500;
  font-size: 14px;
  box-shadow: 0 24px 48px rgba(0,0,0,0.4);
  z-index: 100;
  transform: translateY(20px);
  opacity: 0;
  pointer-events: none;
  transition: var(--transition);
}}

.toast.show {{
  transform: translateY(0);
  opacity: 1;
}}

.toast.error {{
  background: var(--danger);
  color: #fff;
}}

.empty-state {{
  padding: 64px 0;
  text-align: center;
  color: var(--text-muted);
  font-size: 15px;
}}

.empty {{
  display: grid;
  min-height: 250px;
  place-items: center;
  padding: 40px;
  color: var(--text-muted);
  text-align: center;
  font-size: 14px;
}}

.severity {{
  padding: 3px 8px;
  border-radius: 6px;
  font-size: 11px;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: 0.02em;
}}

.severity.low {{
  background: rgba(59, 130, 246, 0.1);
  color: var(--info);
}}

.severity.medium {{
  background: rgba(245, 158, 11, 0.1);
  color: var(--warning);
}}

.severity.high {{
  background: rgba(239, 68, 68, 0.1);
  color: var(--danger);
}}

.severity.critical {{
  background: rgba(239, 68, 68, 0.25);
  border: 1px solid var(--danger);
  color: var(--danger);
}}

.number {{
  font-family: 'JetBrains Mono', monospace;
  font-size: 13px;
  font-weight: 500;
}}
</style>
</head>
<body>

<nav class="floating-nav">
  <div class="brand">
    <div class="mark">H</div>
    <div class="nav-title">HALO Security Console</div>
  </div>
  <div class="connection-pill">
    <span class="dot" id="statusDot"></span>
    <span id="statusText">Kết nối...</span>
  </div>
</nav>

<main class="shell">

  <!-- INGESTION & MONITORING BENTO -->
  <section class="bento-grid">
    <!-- Load Dataset -->
    <div class="col-span-4 card-shell">
      <div class="card-core flex flex-col" style="display: flex; flex-direction: column; gap: 24px;">
        <div>
          <h2 style="font-size: 24px; margin-bottom: 8px;">Nạp dữ liệu</h2>
          <p style="color: var(--text-muted); font-size: 14px;">Chọn file CSV từ thư mục data</p>
        </div>

        <label>
          File nguồn
          <select id="datasetSelect">
            <option value="">Đang tải danh sách...</option>
          </select>
        </label>

        <div style="display: flex; flex-direction: column; gap: 12px;">
          <label class="check-label">
            <input id="saveCache" type="checkbox" checked>
            <span style="margin-left: 12px;">Lưu cache nhị phân (.dat)</span>
          </label>
          <label class="check-label">
            <input id="resumeCheckpoint" type="checkbox" checked>
            <span style="margin-left: 12px;">Bật checkpoint </span>
          </label>
        </div>

        <button class="btn-primary" id="loadButton" type="button" style="margin-top: auto;">
          Nạp Workspace
          <div class="btn-icon-wrapper">
            <svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M5 12h14"></path><path d="m12 5 7 7-7 7"></path></svg>
          </div>
        </button>

        <div class="load-rail" id="loadRail"><div class="load-rail-inner"></div></div>
      </div>
    </div>

    <!-- Active Workspace & Stats -->
    <div class="col-span-8 card-shell" id="monitorPanel">
      <div class="card-core">
        <div class="monitor-header">
          <div>
            <span class="eyebrow-text">WORKSPACE HIỆN TẠI</span>
            <h2 style="font-size: 28px; margin-top: 8px;" id="activeWorkspaceName">Chưa chọn</h2>
          </div>
          <div class="status-badge" id="perfCache">
            <span class="pulse"></span> Sẵn sàng
          </div>
        </div>

        <div style="margin-bottom: 40px;">
          <span style="display: block; font-size: 13px; color: var(--text-muted); margin-bottom: 16px;">WORKSPACE TRONG RAM</span>
          <div class="pill-list" id="loadedPills">
            <span style="color: var(--text-muted); font-style: italic; font-size: 14px;">Chưa có dữ liệu</span>
          </div>
        </div>

        <div class="metric-group">
          <div class="metric">
            <span class="metric-label">Thời gian xử lí</span>
            <span class="metric-value mono" id="perfTime">-</span>
          </div>
          <div class="metric">
            <span class="metric-label">Ram sử dụng</span>
            <span class="metric-value mono" id="perfMemory">-</span>
          </div>
        </div>
      </div>
    </div>

    <!-- Counters -->
    <div class="col-span-full" id="databaseStatsRow" style="display: none;">
      <div class="stat-grid">
        <div class="stat-box"><span>Records</span><strong class="mono" id="records">0</strong></div>
        <div class="stat-box"><span>Users</span><strong class="mono" id="users">0</strong></div>
        <div class="stat-box"><span>Devices</span><strong class="mono" id="devices">0</strong></div>
        <div class="stat-box"><span>Apps</span><strong class="mono" id="apps">0</strong></div>
        <div class="stat-box"><span>Resources</span><strong class="mono" id="resources">0</strong></div>
        <div class="stat-box"><span>Bị bỏ qua</span><strong class="mono" id="skipped">0</strong></div>
        <div class="stat-box"><span>Đã thay thế</span><strong class="mono" id="replaced">0</strong></div>
        <div class="stat-box"><span>Records trùng</span><strong class="mono" id="duplicates">0</strong></div>
      </div>
    </div>
  </section>

  <!-- WORKSPACE OPERATIONS -->
  <section id="workspaceContents" style="display: none;">
    <div class="tabs-container" id="tabsHeader">
      <button class="tab-btn active" id="btnQueryTab">Truy vấn Dữ liệu</button>
      <button class="tab-btn" id="btnAnomalyTab">Phát hiện Bất thường</button>
    </div>

    <!-- QUERY TAB -->
    <div class="tab-pane active" id="paneQuery">
      <div class="query-split">
        <nav class="query-nav">
          <button class="query-nav-btn active" data-subquery="user">
            <strong>Hành trình User</strong>
            <span>Lịch sử hoạt động của người dùng</span>
          </button>
          <button class="query-nav-btn" data-subquery="resource">
            <strong>Tài nguyên truy cập</strong>
            <span>Lịch sử truy cập tài nguyên</span>
          </button>
          <button class="query-nav-btn" data-subquery="top">
            <strong>Top 10 tài nguyên</strong>
            <span>Tài nguyên truy cập nhiều nhất</span>
          </button>
        </nav>

        <div class="card-shell">
          <div class="card-core">

            <div class="query-form-pane active" id="qform-user">
              <h3 style="font-size: 24px; margin-bottom: 32px;">Hành trình người dùng</h3>
              <div style="display: grid; gap: 24px;">
                <label>
                  User ID
                  <input id="userId" placeholder="Ví dụ: U00001">
                </label>
                <div style="display: flex; flex-direction: column; gap: 12px;">
                  <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 24px;">
                    <div>
                      <span style="display: block; font-size: 14px; font-weight: 500; color: var(--text-muted); margin-bottom: 10px;">Thời gian bắt đầu</span>
                      <input id="userStart" type="datetime-local" step="1">
                    </div>
                    <div>
                      <span style="display: block; font-size: 14px; font-weight: 500; color: var(--text-muted); margin-bottom: 10px;">Thời gian kết thúc</span>
                      <input id="userEnd" type="datetime-local" step="1">
                    </div>
                  </div>
                  <div class="time-presets" data-target-start="userStart" data-target-end="userEnd" style="margin-top: 4px;">
                    <button type="button" class="preset-btn" data-preset="all">Tất cả</button>
                    <button type="button" class="preset-btn" data-preset="24h">24h qua</button>
                    <button type="button" class="preset-btn" data-preset="7d">7 ngày qua</button>
                  </div>
                </div>
                <div style="margin-top: 16px;">
                  <button class="btn-primary" data-query="user" type="button">
                    Chạy truy vấn
                    <div class="btn-icon-wrapper"><svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg></div>
                  </button>
                </div>
              </div>
            </div>

            <div class="query-form-pane" id="qform-resource">
              <h3 style="font-size: 24px; margin-bottom: 32px;">Lịch sử tài nguyên</h3>
              <div style="display: grid; gap: 24px;">
                <label>
                  Resource ID
                  <input id="resourceId" placeholder="Ví dụ: R00001">
                </label>
                <div style="display: flex; flex-direction: column; gap: 12px;">
                  <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 24px;">
                    <div>
                      <span style="display: block; font-size: 14px; font-weight: 500; color: var(--text-muted); margin-bottom: 10px;">Thời gian bắt đầu</span>
                      <input id="resourceStart" type="datetime-local" step="1">
                    </div>
                    <div>
                      <span style="display: block; font-size: 14px; font-weight: 500; color: var(--text-muted); margin-bottom: 10px;">Thời gian kết thúc</span>
                      <input id="resourceEnd" type="datetime-local" step="1">
                    </div>
                  </div>
                  <div class="time-presets" data-target-start="resourceStart" data-target-end="resourceEnd" style="margin-top: 4px;">
                    <button type="button" class="preset-btn" data-preset="all">Tất cả</button>
                    <button type="button" class="preset-btn" data-preset="24h">24h qua</button>
                    <button type="button" class="preset-btn" data-preset="7d">7 ngày qua</button>
                  </div>
                </div>
                <div style="margin-top: 16px;">
                  <button class="btn-primary" data-query="resource" type="button">
                    Chạy truy vấn
                    <div class="btn-icon-wrapper"><svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg></div>
                  </button>
                </div>
              </div>
            </div>

            <div class="query-form-pane" id="qform-top">
              <h3 style="font-size: 24px; margin-bottom: 32px;">Top 10 tài nguyên</h3>
              <div style="display: grid; gap: 24px;">
                <div style="display: flex; flex-direction: column; gap: 12px;">
                  <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 24px;">
                    <div>
                      <span style="display: block; font-size: 14px; font-weight: 500; color: var(--text-muted); margin-bottom: 10px;">Thời gian bắt đầu</span>
                      <input id="topStart" type="datetime-local" step="1">
                    </div>
                    <div>
                      <span style="display: block; font-size: 14px; font-weight: 500; color: var(--text-muted); margin-bottom: 10px;">Thời gian kết thúc</span>
                      <input id="topEnd" type="datetime-local" step="1">
                    </div>
                  </div>
                  <div class="time-presets" data-target-start="topStart" data-target-end="topEnd" style="margin-top: 4px;">
                    <button type="button" class="preset-btn" data-preset="all">Tất cả</button>
                    <button type="button" class="preset-btn" data-preset="24h">24h qua</button>
                    <button type="button" class="preset-btn" data-preset="7d">7 ngày qua</button>
                  </div>
                </div>
                <div style="margin-top: 16px;">
                  <button class="btn-primary" data-query="top" type="button">
                    Chạy truy vấn
                    <div class="btn-icon-wrapper"><svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg></div>
                  </button>
                </div>
              </div>
            </div>

          </div>
        </div>
      </div>
    </div>

    <!-- ANOMALY TAB -->
    <div class="tab-pane" id="paneAnomaly">
      <div class="anomaly-split">
        <div class="card-shell">
          <div class="card-core">
            <span class="eyebrow-text">HALO ENGINE</span>
            <h2 style="font-size: 32px; margin-top: 8px;">Phân tích Bất thường</h2>

            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 24px; margin-top: 40px;">
              <label>
                Phân nhóm
                <select id="anomalyCategory">
                  <option value="threshold">Dựa trên ngưỡng</option>
                  <option value="behavior">Dựa trên hành vi</option>
                  <option value="session">Phiên làm việc</option>
                  <option value="advanced">Nâng cao</option>
                </select>
              </label>
              <label>
                Thuật toán
                <select id="anomalyType"></select>
              </label>
            </div>

            <div class="anomaly-desc" id="anomalyDescription"></div>

            <div class="param-grid" id="anomalyParams"></div>

            <div style="margin-top: 32px;">
              <span style="font-size: 14px; font-weight: 500; color: var(--text-muted);">Định dạng kết xuất</span>
              <div class="radio-group">
                <label class="radio-label">
                  <input type="radio" name="output" value="screen" checked>
                  Hiển thị bảng
                </label>
                <label class="radio-label">
                  <input type="radio" name="output" value="file">
                  Ghi file .txt
                </label>
              </div>
            </div>

            <div style="margin-top: 40px; display: flex; align-items: center; gap: 24px;">
              <button class="btn-primary" id="anomalyButton" type="button">
                Bắt đầu phân tích
                <div class="btn-icon-wrapper"><svg width="12" height="12" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><polyline points="9 18 15 12 9 6"></polyline></svg></div>
              </button>
              <div id="fileNote" style="font-size: 14px;"></div>
            </div>
          </div>
        </div>

        <div class="card-shell" style="max-height: 800px; overflow-y: auto;">
          <div class="card-core" style="padding: 24px;">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 24px;">
              <h3 style="font-size: 18px;">Lịch sử báo cáo</h3>
              <div style="display: flex; gap: 16px;">
                <button id="historyRefresh" style="background:transparent; color:var(--accent); font-size:13px; font-weight:500;">Làm mới</button>
                <button id="historyClear" style="background:transparent; color:var(--danger); font-size:13px; font-weight:500;">Xóa hết</button>
              </div>
            </div>
            <div id="historyList"></div>
          </div>
        </div>
      </div>
    </div>

  </section>

  <!-- RESULTS -->
  <section class="card-shell" id="resultsSection" style="display: none; margin-top: -32px;">
    <div class="card-core" style="padding: 0; overflow: hidden;">
      <div style="padding: 32px; border-bottom: 1px solid var(--panel-border); display: flex; justify-content: space-between; align-items: center;">
        <h3 style="font-size: 20px;" id="resultsTitle">Kết quả truy vấn</h3>
        <span class="mono" style="color: var(--text-muted); font-size: 14px;" id="resultsMeta">0 kết quả</span>
      </div>
      <div class="table-wrapper" id="resultsBody">
        <div class="empty-state">Kết quả sẽ hiển thị ở đây.</div>
      </div>
    </div>
  </section>

</main>

<div class="toast" id="toast"></div>

<script>
{script_content}
</script>
</body>
</html>"""

    with open(input_path, 'w', encoding='utf-8') as f:
        f.write(new_html)

if __name__ == '__main__':
    rewrite()
