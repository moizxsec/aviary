'use strict';
window.PX = window.PX || {};

(function (NS) {
  const { rand, lerp, clamp } = NS;

  // An irregular burnt edge. Straight sawtooth reads as pinking shears, so the
  // depth is built from several frequencies plus the occasional deep bite where
  // the fire ate further in. Regenerated per letter, so no two are alike.
  function charredPolygon(steps = 46) {
    const pts = [];
    const push = (x, y) => pts.push(`${x.toFixed(2)}% ${y.toFixed(2)}%`);

    const seedA = rand(100), seedB = rand(100), seedC = rand(100);
    function depth(t, edge) {
      const p = t * steps;
      let d =
        1.5 +
        Math.sin(p * 0.31 + seedA + edge * 2.3) * 1.5 +
        Math.sin(p * 0.87 + seedB + edge * 1.1) * 0.9 +
        Math.sin(p * 2.10 + seedC + edge * 3.7) * 0.5 +
        rand(-0.5, 0.5);
      if (Math.random() < 0.06) d += rand(1.8, 4.2);   // a deep bite
      return clamp(d, 0.2, 7.5);
    }

    // top
    for (let i = 0; i <= steps; i++) push(lerp(0, 100, i / steps), depth(i / steps, 0));
    // right
    for (let i = 1; i <= steps; i++) push(100 - depth(i / steps, 1) * 0.62, lerp(0, 100, i / steps));
    // bottom
    for (let i = 1; i <= steps; i++) push(lerp(100, 0, i / steps), 100 - depth(i / steps, 2));
    // left
    for (let i = 1; i < steps; i++) push(depth(i / steps, 3) * 0.62, lerp(100, 0, i / steps));

    return `polygon(${pts.join(',')})`;
  }

  class Letter {
    constructor(root) {
      this.root = root;
      this.el = null;
      this.open = false;
      this.onDismiss = null;
      this._keyHandler = (e) => {
        if (e.key === 'Escape') this.dismiss();
      };
    }

    // returns the DOM rect the scroll should land on
    plannedRect() {
      const w = Math.min(520, window.innerWidth * 0.62);
      const h = Math.min(360, window.innerHeight * 0.5);
      return {
        x: (window.innerWidth - w) / 2,
        y: window.innerHeight * 0.54 - h / 2,
        w, h
      };
    }

    show(letter) {
      this.hide();

      const wrap = document.createElement('div');
      wrap.className = 'letter';
      wrap.style.clipPath = charredPolygon();

      const grain = document.createElement('div');
      grain.className = 'letter-grain';

      const scorch = document.createElement('div');
      scorch.className = 'letter-scorch';

      const body = document.createElement('div');
      body.className = 'letter-body';

      const seal = document.createElement('div');
      seal.className = 'letter-seal';
      seal.innerHTML =
        '<svg viewBox="0 0 24 24" aria-hidden="true">' +
        '<path d="M12 2.6c.9 3.5-1.6 4.4-2.3 6.6-.6 1.9.6 3.2.6 3.2s-.9-.3-1.5-1.3' +
        'c-.5 1-1 2-1 3.3 0 3 2.4 5.4 5.4 5.4s5.4-2.4 5.4-5.6c0-4.3-3.6-6-4.3-8.6' +
        '-.3-1.2-.1-2.3-.1-2.3S13.8 4.4 12 2.6Z"/></svg>';

      const text = document.createElement('p');
      text.className = 'letter-text';
      text.textContent = letter.text;

      body.appendChild(seal);
      body.appendChild(text);

      if (letter.from) {
        const from = document.createElement('p');
        from.className = 'letter-from';
        from.textContent = '— ' + letter.from;
        body.appendChild(from);
      }

      const hint = document.createElement('button');
      hint.className = 'letter-hint';
      hint.type = 'button';
      hint.textContent = 'let it go';
      hint.addEventListener('click', () => this.dismiss());
      body.appendChild(hint);

      wrap.appendChild(grain);
      wrap.appendChild(scorch);
      wrap.appendChild(body);

      const r = this.plannedRect();
      wrap.style.left = r.x + 'px';
      wrap.style.top = r.y + 'px';
      wrap.style.width = r.w + 'px';
      wrap.style.minHeight = Math.min(220, r.h) + 'px';

      this.root.appendChild(wrap);
      this.el = wrap;
      this.open = true;

      // unfurl: a thin line that opens downward
      requestAnimationFrame(() => wrap.classList.add('is-open'));

      window.addEventListener('keydown', this._keyHandler);

      this.autoFade = setTimeout(() => this.dismiss(), 1000 * 180);
      return r;
    }

    // called at the moment the phoenix goes up, to singe the paper
    scorch() {
      if (this.el) this.el.classList.add('is-scorched');
    }

    rect() {
      return this.el ? this.el.getBoundingClientRect() : null;
    }

    dismiss() {
      if (!this.el || !this.open) return;
      this.open = false;
      clearTimeout(this.autoFade);
      window.removeEventListener('keydown', this._keyHandler);
      this.el.classList.add('is-gone');
      const el = this.el;
      setTimeout(() => el.remove(), 900);
      this.el = null;
      if (this.onDismiss) this.onDismiss();
    }

    hide() {
      clearTimeout(this.autoFade);
      window.removeEventListener('keydown', this._keyHandler);
      if (this.el) this.el.remove();
      this.el = null;
      this.open = false;
    }
  }

  NS.Letter = Letter;
  NS.charredPolygon = charredPolygon;
})(window.PX);
