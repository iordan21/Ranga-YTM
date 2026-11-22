// Ranga YTM: o controle dentro da página do YouTube Music.
//
// O app nativo (main.cpp) chama window.__ranga.<ação>() quando um botão do
// controle é apertado. O direcional anda pelo que dá para apertar na página
// (links e botões), escolhendo o mais perto na direção pedida; A aperta o que
// está em foco. Nada aqui lê ou muda a conta, e nada pula propaganda.
(() => {
  if (window.__ranga) return;

  // ---- O que está tocando: para o app (o aviso no canto e o mini player) ----
  // Uma olhada por segundo na barra do player, só na página de cima (não nos
  // quadros do login): título, artista, capa, se está tocando e o tempo. O app
  // é que vê quando a música mudou. Mensagem só quando algo mudou.
  if (window.top === window && window.chrome && chrome.webview) {
    let last = '';
    setInterval(() => {
      const bar = document.querySelector('ytmusic-player-bar');
      if (!bar) return;
      const title = ((bar.querySelector('.title') || {}).textContent || '').trim();
      if (!title) return;
      const byline = ((bar.querySelector('.byline') || {}).textContent || '').split('•')[0].trim();
      const img = bar.querySelector('img.image') || bar.querySelector('img');
      // A capa em 120 px (a da barra vem em 60).
      const art = img && img.src ? img.src.replace(/=w\d+-h\d+/, '=w120-h120') : '';
      const media = document.querySelector('video');
      const playing = media && !media.paused ? '1' : '0';
      const position = media ? Math.floor(media.currentTime || 0) : 0;
      const length = media && isFinite(media.duration) ? Math.floor(media.duration) : 0;
      const message = ['estado', title, byline, art, playing, position, length].join('\n');
      if (message === last) return;
      last = message;
      chrome.webview.postMessage(message);
    }, 1000);
  }

  const css = document.createElement('style');
  css.textContent =
    // Sombra por dentro, e não só contorno: a barra lateral corta o que passa da borda dela.
    '.ranga-foco{outline:3px solid #3ddc84 !important;outline-offset:-3px !important;' +
    'box-shadow:inset 0 0 0 3px #3ddc84, 0 0 18px rgba(61,220,132,.45) !important;border-radius:10px !important}';
  const addCss = () => (document.head || document.documentElement).appendChild(css);
  if (document.head) addCss(); else document.addEventListener('DOMContentLoaded', addCss);

  const SELECTOR = [
    'a[href]', 'button', '[role="button"]', '[role="tab"]', '[role="option"]',
    'tp-yt-paper-icon-button', 'tp-yt-paper-tab', 'tp-yt-paper-item',
    'ytmusic-chip-cloud-chip-renderer', 'input',
  ].join(',');

  let current = null;

  const visible = (el) => {
    const r = el.getBoundingClientRect();
    if (r.width < 4 || r.height < 4) return false;
    if (r.bottom < -200 || r.top > innerHeight + 200) return false;
    const s = getComputedStyle(el);
    return s.visibility !== 'hidden' && s.display !== 'none' && s.opacity !== '0';
  };

  // O que ganha a borda: o cartão ou a linha inteira em volta do link, que de
  // longe (na TV) é o que se enxerga; o foco continua no link.
  const ringOf = (el) =>
    el.closest('ytmusic-two-row-item-renderer, ytmusic-responsive-list-item-renderer, ytmusic-guide-entry-renderer') || el;
  let ring = null;

  const mark = (el) => {
    if (ring) ring.classList.remove('ranga-foco');
    current = el;
    ring = el ? ringOf(el) : null;
    if (!el) return;
    ring.classList.add('ranga-foco');
    if (typeof el.focus === 'function') el.focus({ preventScroll: true });
    el.scrollIntoView({ block: 'nearest', inline: 'nearest', behavior: 'smooth' });
  };

  const center = (r) => ({ x: r.left + r.width / 2, y: r.top + r.height / 2 });

  // O mais perto na direção: distância no eixo andado + o dobro do desvio no outro.
  const move = (dir) => {
    const all = [...document.querySelectorAll(SELECTOR)].filter(visible);
    if (!all.length) return;
    if (!current || !current.isConnected || !visible(current)) {
      // Primeira vez (ou a página mudou): o primeiro cartão ou música que aparece,
      // que é o que se quer no sofá; sem nenhum, o primeiro item da tela.
      const onScreen = all.filter((el) => el.getBoundingClientRect().top >= 0);
      mark(onScreen.find((el) => el.closest('ytmusic-two-row-item-renderer, ytmusic-responsive-list-item-renderer')) ||
           onScreen[0] || all[0]);
      return;
    }
    const from = center(current.getBoundingClientRect());
    let best = null, bestScore = Infinity;
    for (const el of all) {
      if (el === current || current.contains(el) || el.contains(current)) continue;
      const to = center(el.getBoundingClientRect());
      const dx = to.x - from.x, dy = to.y - from.y;
      let main, side;
      if (dir === 'cima') { main = -dy; side = Math.abs(dx); }
      else if (dir === 'baixo') { main = dy; side = Math.abs(dx); }
      else if (dir === 'esquerda') { main = -dx; side = Math.abs(dy); }
      else { main = dx; side = Math.abs(dy); }
      if (main <= 2) continue;
      const score = main + side * 2;
      if (score < bestScore) { bestScore = score; best = el; }
    }
    if (best) mark(best);
    else if (dir === 'baixo') scrollBy({ top: innerHeight * 0.6, behavior: 'smooth' });
    else if (dir === 'cima') scrollBy({ top: -innerHeight * 0.6, behavior: 'smooth' });
  };

  const click = (selector) => {
    const el = document.querySelector(selector);
    if (el) el.click();
    return !!el;
  };

  // ---- O teclado na tela ----
  // Para digitar com o controle (o e-mail do login, a busca): abre com A num
  // campo de texto. As letras entram no campo pelo mesmo caminho de uma
  // colagem (insertText), que a página aceita como digitação; nada é guardado
  // nem mandado para outro lugar.
  const ROWS = [
    ['1', '2', '3', '4', '5', '6', '7', '8', '9', '0'],
    ['q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p'],
    ['a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', '@'],
    ['z', 'x', 'c', 'v', 'b', 'n', 'm', '.', '-', '_'],
    ['⇧ Maiúsc', 'Espaço', '⌫ Apagar', '.com', '✓ Pronto'],
  ];
  const kb = { open: false, row: 1, col: 0, upper: false, field: null, el: null };

  const isField = (el) =>
    !!el && ((el.tagName === 'INPUT' && !['button', 'submit', 'checkbox', 'radio', 'hidden'].includes((el.type || '').toLowerCase())) ||
             el.tagName === 'TEXTAREA' || el.isContentEditable);

  const drawKeyboard = () => {
    if (!kb.el) {
      kb.el = document.createElement('div');
      kb.el.style.cssText =
        'position:fixed;left:50%;bottom:24px;transform:translateX(-50%);z-index:2147483647;background:#151515;' +
        'border:1px solid #333;border-radius:16px;padding:14px;box-shadow:0 10px 40px rgba(0,0,0,.6);' +
        'font:500 18px Roboto,Arial,sans-serif;color:#fff;user-select:none';
      document.documentElement.appendChild(kb.el);
    }
    // replaceChildren, e não innerHTML: o YouTube exige Trusted Types e recusa
    // innerHTML (o teclado aparecia vazio).
    kb.el.replaceChildren();
    ROWS.forEach((row, r) => {
      const line = document.createElement('div');
      line.style.cssText = 'display:flex;gap:8px;justify-content:center;margin:6px 0';
      row.forEach((key, c) => {
        const cell = document.createElement('div');
        const wide = key.length > 2;
        let label = key;
        if (!wide && kb.upper) label = key.toUpperCase();
        if (key === '⇧ Maiúsc' && kb.upper) label = '⇧ MAIÚSC';
        cell.textContent = label;
        const on = r === kb.row && c === kb.col;
        cell.style.cssText =
          'min-width:' + (wide ? 120 : 44) + 'px;height:44px;display:flex;align-items:center;justify-content:center;' +
          'border-radius:10px;padding:0 10px;background:' + (on ? '#3ddc84' : '#2a2a2a') + ';color:' + (on ? '#000' : '#fff');
        line.appendChild(cell);
      });
      kb.el.appendChild(line);
    });
    const hint = document.createElement('div');
    hint.textContent = 'Direcional: tecla   A: digitar   B: fechar';
    hint.style.cssText = 'text-align:center;color:#999;font-size:13px;margin-top:8px';
    kb.el.appendChild(hint);
  };

  const openKeyboard = (field) => {
    kb.open = true;
    kb.field = field;
    kb.row = 1;
    kb.col = 0;
    field.focus();
    drawKeyboard();
  };

  const closeKeyboard = () => {
    kb.open = false;
    if (kb.el) { kb.el.remove(); kb.el = null; }
  };

  const type = (text) => {
    if (!kb.field || !kb.field.isConnected) return;
    kb.field.focus();
    document.execCommand('insertText', false, text);
  };

  const pressKey = () => {
    const key = ROWS[kb.row][kb.col];
    if (key === '⇧ Maiúsc') kb.upper = !kb.upper;
    else if (key === 'Espaço') type(' ');
    else if (key === '⌫ Apagar') { kb.field && kb.field.focus(); document.execCommand('delete'); }
    else if (key === '.com') type('.com');
    else if (key === '✓ Pronto') {
      // Na busca do YouTube Music, Pronto já pesquisa; no login, só fecha
      // (aí é andar até "Avançar" e apertar A).
      const field = kb.field;
      closeKeyboard();
      if (field && field.closest && field.closest('ytmusic-search-box') && field.value.trim())
        location.href = '/search?q=' + encodeURIComponent(field.value.trim());
      return;
    }
    else type(kb.upper ? key.toUpperCase() : key);
    drawKeyboard();
  };

  const moveKey = (dir) => {
    if (dir === 'cima') kb.row = (kb.row + ROWS.length - 1) % ROWS.length;
    else if (dir === 'baixo') kb.row = (kb.row + 1) % ROWS.length;
    else if (dir === 'esquerda') kb.col -= 1;
    else kb.col += 1;
    const width = ROWS[kb.row].length;
    kb.col = ((kb.col % width) + width) % width;
    drawKeyboard();
  };

  window.__ranga = {
    move(dir) {
      if (kb.open) moveKey(dir);
      else move(dir);
    },
    accept() {
      if (kb.open) return pressKey();
      if (current && current.isConnected) {
        if (isField(current)) return openKeyboard(current);
        current.click();
      } else move('baixo');
    },
    // B: fecha o teclado se ele estiver aberto (true: o B foi usado aqui).
    back() {
      if (!kb.open) return false;
      closeKeyboard();
      return true;
    },
    // Os botões da barra do player, pelos nomes que a página usa.
    playPause() { click('ytmusic-player-bar #play-pause-button') || click('#play-pause-button'); },
    next() { click('ytmusic-player-bar .next-button') || click('.next-button'); },
    previous() { click('ytmusic-player-bar .previous-button') || click('.previous-button'); },
    // Abre e fecha a tela grande do que está tocando.
    togglePlayer() { click('ytmusic-player-bar .toggle-player-page-button') || click('ytmusic-player-bar'); },
    search() {
      click('ytmusic-search-box #placeholder') || click('ytmusic-search-box');
      setTimeout(() => {
        const input = document.querySelector('ytmusic-search-box input');
        if (input) { mark(input); openKeyboard(input); }
      }, 150);
    },
  };
})();
