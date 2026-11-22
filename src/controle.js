// Ranga YTM: o controle dentro da página do YouTube Music.
//
// O app nativo (main.cpp) chama window.__ranga.<ação>() quando um botão do
// controle é apertado. O direcional anda pelo que dá para apertar na página
// (links e botões), escolhendo o mais perto na direção pedida; A aperta o que
// está em foco. Nada aqui lê ou muda a conta, e nada pula propaganda.
(() => {
  if (window.__ranga) return;

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

  window.__ranga = {
    move,
    accept() {
      if (current && current.isConnected) current.click();
      else move('baixo');
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
        if (input) { mark(input); input.focus(); }
      }, 150);
    },
  };
})();
