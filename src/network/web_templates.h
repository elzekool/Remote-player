#pragma once

// HTML templates for the web pages. {{NAME}} placeholders are filled in by
// web.cpp while the page is sent (see html_writer.h); everything else is sent
// as written. The templates stay in flash, so their size costs no RAM.

// The frame around every page: Material-style app bar, navigation and
// stylesheet.
// {{TITLE}}: page title; {{NAV}}: the navigation links (NAV_TEMPLATE), if any;
// {{CONTENT}}: the page itself.
static const char LAYOUT_TEMPLATE[] = R"html(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{{TITLE}} - Remote player</title>
<style>
:root {
  --primary: #1565c0; --on-primary: #fff; --surface: #fff; --background: #f2f4f7;
  --text: #1f2328; --muted: #6b7280; --divider: #e3e6ea;
  --ok: #2e7d32; --ok-bg: #e8f5e9; --err: #c62828; --err-bg: #fdecea;
  --busy: #1565c0; --busy-bg: #e3f2fd; --idle-bg: #eceff1;
}
* { box-sizing: border-box; }
body { margin: 0; background: var(--background); color: var(--text);
  font: 14px/1.5 Roboto, system-ui, -apple-system, "Segoe UI", sans-serif; }
a { color: var(--primary); }
header { background: var(--primary); color: var(--on-primary); position: sticky; top: 0;
  z-index: 1; box-shadow: 0 2px 4px rgba(0,0,0,.2); }
header .bar { max-width: 768px; margin: 0 auto; padding: 0 16px; height: 56px;
  display: flex; align-items: center; gap: 4px; }
header h1 { flex: 1; margin: 0; font-size: 20px; font-weight: 500; white-space: nowrap;
  overflow: hidden; text-overflow: ellipsis; }
header nav a { color: var(--on-primary); text-decoration: none; padding: 8px 12px;
  border-radius: 4px; font-size: 13px; font-weight: 500; letter-spacing: .06em;
  text-transform: uppercase; opacity: .8; }
header nav a:hover { background: rgba(255,255,255,.12); }
header nav a.active { opacity: 1; background: rgba(255,255,255,.18); }
main { max-width: 768px; margin: 0 auto; padding: 16px; }
.card { background: var(--surface); border-radius: 8px; padding: 16px 20px; margin-bottom: 16px;
  box-shadow: 0 1px 3px rgba(0,0,0,.12), 0 1px 2px rgba(0,0,0,.08); }
.card h2 { margin: 0 0 12px; font-size: 16px; font-weight: 500; }
.card h3 { margin: 20px 0 0; font-size: 13px; font-weight: 500; color: var(--primary);
  text-transform: uppercase; letter-spacing: .06em; }
.narrow { max-width: 420px; }
hr { border: 0; border-top: 1px solid var(--divider); margin: 24px 0 0; }
td code, td i { white-space: nowrap; }
.hint { color: var(--muted); font-size: 12px; }
.status { display: grid; grid-template-columns: max-content 1fr; gap: 8px 16px; align-items: center; }
.status .name { color: var(--muted); }
.chip { display: inline-block; padding: 1px 10px; border-radius: 12px; font-size: 12px;
  font-weight: 500; background: var(--idle-bg); white-space: nowrap; }
.chip.ok { background: var(--ok-bg); color: var(--ok); }
.chip.err { background: var(--err-bg); color: var(--err); }
.chip.busy { background: var(--busy-bg); color: var(--busy); }
label { display: block; margin-top: 14px; font-size: 12px; color: var(--muted); }
input { display: block; width: 100%; margin-top: 4px; padding: 9px 12px; font: inherit;
  font-size: 14px; color: var(--text); background: var(--surface);
  border: 1px solid #c4c9cf; border-radius: 4px; }
input:focus { outline: none; border-color: var(--primary); box-shadow: 0 0 0 1px var(--primary); }
input[type=file] { padding: 7px; }
label.check { display: flex; align-items: center; gap: 8px; margin-top: 8px; }
label.check input { width: auto; margin: 0; }
button { margin-top: 16px; padding: 8px 18px; border: 0; border-radius: 4px; cursor: pointer;
  background: var(--primary); color: var(--on-primary); font: inherit; font-size: 13px;
  font-weight: 500; letter-spacing: .06em; text-transform: uppercase;
  box-shadow: 0 1px 3px rgba(0,0,0,.25); }
button:hover { box-shadow: 0 2px 6px rgba(0,0,0,.3); }
button.text { margin: 0; padding: 4px 8px; background: none; color: var(--primary); box-shadow: none; }
button.text:hover { background: var(--busy-bg); }
button.text.danger { color: var(--err); }
button.text.danger:hover { background: var(--err-bg); }
form.inline { display: inline; margin: 0; }
.table { overflow-x: auto; }
table { width: 100%; border-collapse: collapse; }
th { text-align: left; font-size: 12px; font-weight: 500; color: var(--muted); }
th, td { padding: 8px; border-bottom: 1px solid var(--divider); }
tr:last-child td { border-bottom: 0; }
td.actions { text-align: right; white-space: nowrap; }
code { background: var(--idle-bg); padding: 0 4px; border-radius: 3px; font-size: 13px; }
pre { margin: 12px 0 0; padding: 12px; border-radius: 4px; background: #263238; color: #eceff1;
  white-space: pre-wrap; font-size: 13px; }
.notice { margin-bottom: 16px; padding: 12px 16px; border-radius: 4px; background: #323232;
  color: #fff; box-shadow: 0 3px 5px rgba(0,0,0,.2); }
footer { max-width: 768px; margin: 0 auto; padding: 0 20px 24px; color: var(--muted); font-size: 12px; }
</style>
</head>
<body>
<header><div class="bar">
  <h1>Remote player</h1>
  {{NAV}}
</div></header>
{{CONTENT}}
</body>
</html>
)html";

// The navigation links. {{STATUS_NAV}} / {{FILES_NAV}} / {{USAGE_NAV}}:
// "active" for the current page.
static const char NAV_TEMPLATE[] = R"html(<nav><a href="/" class="{{STATUS_NAV}}">Status</a><a href="/files" class="{{FILES_NAV}}">Files</a><a href="/usage" class="{{USAGE_NAV}}">Usage</a></nav>)html";

// The status page. {{PLAYER_ROWS}} adds a table row per configured unit.
static const char ROOT_TEMPLATE[] = R"html(<main>
<section class="card">
  <h2>Status</h2>
  <div class="status">
    <span class="name">WiFi</span><span>{{WIFI_STATUS}}</span>
    <span class="name">MQTT</span><span>{{MQTT_STATUS}}</span>
    <span class="name">SD card</span><span>{{CARD_STATUS}} <a href="/files">Manage files</a></span>
    <span class="name">OTA updates</span><span>{{OTA_STATUS}}</span>
  </div>
</section>

<section class="card">
  <h2>Players</h2>
  <div class="table"><table>
    <tr><th>Unit</th><th>Status</th><th>Track</th><th>Volume</th><th>Repeat</th><th>Pins (BCLK/LRC/DIN)</th></tr>
    {{PLAYER_ROWS}}
  </table></div>
</section>

<section class="card">
  <h2>Command</h2>
  <form method="POST" action="/command">
    <label>Command
      <input name="cmd" value="{{COMMAND}}" autocomplete="off" placeholder="e.g. 2:play 4">
    </label>
    <div class="hint">e.g. <code>2:play 4</code>, <code>play "hello"</code>,
      <code>play 1,"hello",6</code> (random pick) or <code>all:stop</code>. See
      <a href="/usage">Usage</a> for all commands and how to send them over MQTT or HTTP.</div>
    <button type="submit">Send</button>
  </form>
  {{COMMAND_OUTPUT}}
</section>

<section class="card">
  <h2>Settings</h2>
  <form method="POST" action="/save" class="narrow">
    <h3>WiFi</h3>
    <label>SSID<input name="ssid" value="{{SSID}}"></label>
    <label>Password<input name="wifi_pass" type="password" placeholder="(unchanged)"></label>
    <hr>
    <h3>Device</h3>
    <label>Hostname<input name="hostname" value="{{HOSTNAME}}"></label>
    <label>OTA password<input name="ota_pass" type="password" placeholder="{{OTA_PLACEHOLDER}}"></label>
    {{OTA_CLEAR}}
    <label>Web password (user <code>{{WEB_USER}}</code>)<input name="web_pass" type="password" placeholder="{{WEB_PLACEHOLDER}}"></label>
    {{WEB_CLEAR}}
    <label>Number of players (1-{{MAX_PLAYERS}})
      <input name="players" type="number" min="1" max="{{MAX_PLAYERS}}" value="{{PLAYER_COUNT}}">
    </label>
    <hr>
    <h3>MQTT</h3>
    <label>Broker hostname (host or host:port; empty disables MQTT)
      <input name="mqtt_host" value="{{MQTT_HOST}}">
    </label>
    <label>Username<input name="mqtt_user" value="{{MQTT_USER}}"></label>
    <label>Password<input name="mqtt_pass" type="password" placeholder="(unchanged)"></label>
    <label>Topic<input name="mqtt_topic" value="{{MQTT_TOPIC}}"></label>
    <button type="submit">Save &amp; restart</button>
  </form>
</section>
</main>
<footer>
  Audio CPU load: {{CPU_LOAD}}% (peak {{PEAK_LOAD}}%), of which SD card reading {{CARD_LOAD}}%<br>
  Free heap: {{FREE_HEAP}} KB of {{TOTAL_HEAP}} KB<br>
  Firmware built {{BUILD_DATE}}
</footer>
)html";

// The file manager. {{MESSAGE}} is the result of the last action, if any;
// {{FILE_LIST}} is the table of files (or a note that there is no card).
static const char FILES_TEMPLATE[] = R"html(<main>
{{MESSAGE}}
<section class="card">
  <h2>Files on the SD card</h2>
  {{FILE_LIST}}
</section>

<section class="card">
  <h2>Upload</h2>
  <form method="POST" action="/files/upload" enctype="multipart/form-data" class="narrow">
    <label>File<input type="file" name="file" required></label>
    <div class="hint">Stored in the root of the card; a file with the same name is replaced.
      Track 14 plays <code>0014.wav</code> or <code>0014.mp3</code>; track <code>"hello"</code>
      plays <code>hello.wav</code> or <code>hello.mp3</code>.</div>
    <button type="submit">Upload</button>
  </form>
</section>
</main>
)html";

// One row of the file table. The buttons read the file name from their form, so
// no name ends up inside a script. Rename asks for the name without the
// extension, which stays the same.
static const char FILE_ROW_TEMPLATE[] =
    R"html(<tr><td>{{NAME}}</td><td>{{SIZE}}</td><td class="actions">
  <form method="POST" action="/files/rename" class="inline"
    onsubmit="var f=this.elements['name'].value,d=f.lastIndexOf('.'),x=d>0?f.slice(d):'';var n=prompt(x?'New name (the extension '+x+' stays)':'New name',x?f.slice(0,d):f);if(n===null)return false;n=n.trim();if(x&&n.toLowerCase().endsWith(x.toLowerCase()))n=n.slice(0,-x.length);n+=x;if(n===f)return false;this.elements['to'].value=n;return true">
    <input type="hidden" name="name" value="{{NAME}}"><input type="hidden" name="to">
    <button type="submit" class="text">Rename</button>
  </form>
  <form method="POST" action="/files/delete" class="inline"
    onsubmit="return confirm('Delete '+this.elements['name'].value+'?')">
    <input type="hidden" name="name" value="{{NAME}}">
    <button type="submit" class="text danger">Delete</button>
  </form>
</td></tr>
)html";

// The command reference. The limits come from the code, so they stay correct.
static const char USAGE_TEMPLATE[] = R"html(<main>
<section class="card">
  <h2>Sending commands</h2>
  <p>Send commands as MQTT messages to the topic <code>{{MQTT_TOPIC}}</code> (when MQTT is
    configured), as HTTP requests (see below), or type them on the <a href="/">Status</a> page. A command has the form <code>[unit:]command [arguments]</code>;
    command names are not case-sensitive.</p>
  <div class="table"><table>
    <tr><th>Prefix</th><th>Sends the command to</th></tr>
    <tr><td><i>none</i></td><td>Unit 1</td></tr>
    <tr><td><code>2:</code></td><td>That unit (1-{{MAX_PLAYERS}})</td></tr>
    <tr><td><code>all:</code></td><td>Every configured unit</td></tr>
  </table></div>
</section>

<section class="card">
  <h2>Sending commands over HTTP</h2>
  <p>POST the command as the form field <code>cmd</code> to <code>/command</code>. This works
    with or without MQTT. The response is the command's log output as plain text.</p>
  <pre>curl --data-urlencode "cmd=2:play 4" http://{{HOSTNAME}}.local/command</pre>
  <p>With a web password, add the user and password:</p>
  <pre>curl -u {{WEB_USER}}:PASSWORD --data-urlencode "cmd=2:play 4" http://{{HOSTNAME}}.local/command</pre>
</section>

<section class="card">
  <h2>Tracks</h2>
  <p>Commands that play something take one track, or a list to pick from at random.</p>
  <div class="table"><table>
    <tr><th>Written as</th><th>Plays</th></tr>
    <tr><td><code>14</code></td><td><code>/0014.wav</code>, or <code>/0014.mp3</code> if there is no WAV file</td></tr>
    <tr><td><code>"hello"</code></td><td><code>/hello.wav</code>, or <code>/hello.mp3</code> if there is no WAV file</td></tr>
    <tr><td><code>1,"hello",6</code></td><td>One of them, picked at random each time</td></tr>
  </table></div>
  <p class="hint">Numbers run from 1 to {{MAX_TRACK_NUMBER}}. Names are up to {{MAX_TRACK_NAME}}
    characters, without the extension, and may contain spaces and commas but none of
    <code>/ \ : * ? " &lt; &gt; |</code>. A list holds up to {{MAX_TRACK_CHOICES}} tracks, with no
    spaces after the commas. Files are read from the root of the SD card; WAV and MP3 files of
    any sample rate work.</p>
</section>

<section class="card">
  <h2>Commands</h2>
  <div class="table"><table>
    <tr><th>Command</th><th>What it does</th></tr>
    <tr><td><code>play TRACKS</code></td><td>Plays a track once.</td></tr>
    <tr><td><code>loop TRACKS</code></td><td>Plays a track over and over until stopped.</td></tr>
    <tr><td><code>repeat TRACKS [COUNT]</code></td><td>Plays COUNT tracks one after the other,
      picking a new one from the list each time (up to {{MAX_REPEAT_COUNT}}). Without COUNT, or
      with 0, it continues until stopped.</td></tr>
    <tr><td><code>replace TRACKS [TRANSITION]</code></td><td>Changes the tracks a running repeat
      picks from, from the next track on. With TRANSITION, that track plays first; it doesn't count
      towards COUNT, and any other command before it starts cancels it.</td></tr>
    <tr><td><code>delayed TRACKS MS</code></td><td>Plays a track after MS milliseconds
      (0-{{MAX_DELAY_MS}}), unless another command for the unit comes first.</td></tr>
    <tr><td><code>stop</code></td><td>Stops playback, including a loop or repeat.</td></tr>
    <tr><td><code>volume N</code></td><td>Sets the volume from 0 (silent) to {{MAX_VOLUME}}.
      Kept after a restart.</td></tr>
    <tr><td><code>disable</code></td><td>Stops playback and ignores commands that play
      something, until enabled again. Kept after a restart.</td></tr>
    <tr><td><code>enable</code></td><td>Allows playback again.</td></tr>
  </table></div>
  <p class="hint">A command that plays something replaces whatever the unit was playing. Every
    command cancels a waiting delayed track.</p>
</section>

<section class="card">
  <h2>Examples</h2>
  <div class="table"><table>
    <tr><td><code>play 4</code></td><td>Unit 1 plays track 4</td></tr>
    <tr><td><code>2:loop "chime"</code></td><td>Unit 2 plays <code>chime</code> until stopped</td></tr>
    <tr><td><code>3:repeat 1,2,3 5</code></td><td>Unit 3 plays five tracks, each picked from 1, 2 and 3</td></tr>
    <tr><td><code>replace "loop2" "loop1to2"</code></td><td>After the current track, unit 1 plays <code>loop1to2</code>, then repeats <code>loop2</code></td></tr>
    <tr><td><code>4:delayed "doors closing" 1500</code></td><td>Unit 4 plays <code>doors closing</code> after 1.5 seconds</td></tr>
    <tr><td><code>all:volume 20</code></td><td>Sets every unit to volume 20</td></tr>
    <tr><td><code>all:stop</code></td><td>Stops every unit</td></tr>
  </table></div>
</section>
</main>
)html";

// The only page in access point mode when a web password is set: just the WiFi
// settings, without login.
static const char WIFI_ONLY_TEMPLATE[] = R"html(<main>
<section class="card">
  <h2>WiFi</h2>
  <p>The device is in access point mode. Enter the WiFi network to connect to.
    Everything else needs the web password and is available once the device is
    connected.</p>
  <form method="POST" action="/save" class="narrow">
    <label>SSID<input name="ssid" value="{{SSID}}"></label>
    <label>Password<input name="wifi_pass" type="password" placeholder="(unchanged)"></label>
    <button type="submit">Save &amp; restart</button>
  </form>
</section>
</main>
)html";

// Shown after saving the settings, while the device restarts.
static const char SAVED_TEMPLATE[] = R"html(<main>
<section class="card">
  <h2>Settings saved</h2>
  <p>The device is restarting&hellip;</p>
  {{RECONNECT}}
</section>
</main>
)html";
