// Shared RAL Classic colour picker for the v2 dashboard + admin pages.
// Lazy-loads data/ral-classic.json on first open (no cost when the user never
// touches the RAL button) and filters across code / Dutch / English names.
//
// Wire up via initRalPicker({ openBtn, modal, closeBtn, search, list, onPick })
// where onPick(hex) calls the page's existing applyColor flow.
(function () {
  const RAL_JSON_URL = '/ral-classic.json';
  let cachedEntries = null; // [{code, key, hex, nl, en, searchIndex}, ...]
  let fetchPromise = null;

  function T(k, fb) {
    return window.chronolettI18n ? window.chronolettI18n.t(k, fb) : fb;
  }

  function loadEntries() {
    if (cachedEntries) return Promise.resolve(cachedEntries);
    if (fetchPromise) return fetchPromise;
    fetchPromise = fetch(RAL_JSON_URL, { cache: 'force-cache' })
      .then((r) => {
        if (!r.ok) throw new Error('HTTP ' + r.status);
        return r.json();
      })
      .then((obj) => {
        cachedEntries = Object.keys(obj).map((key) => {
          const v = obj[key];
          // Pre-compute a lowercase searchable string per entry so the input
          // handler stays cheap even at 205 rows × every keystroke.
          const code = key.replace(/^RAL\s*/, '');
          return {
            code,
            key,
            hex: v.hex,
            nl: v.nl,
            en: v.en,
            searchIndex: (code + ' ' + v.nl + ' ' + v.en).toLowerCase(),
          };
        });
        return cachedEntries;
      })
      .catch((err) => {
        fetchPromise = null; // allow retry on next open
        throw err;
      });
    return fetchPromise;
  }

  function render(listEl, entries, onPick) {
    listEl.textContent = '';
    if (!entries.length) {
      const li = document.createElement('li');
      li.className = 'ral-empty';
      li.textContent = T('dashboard.colour.ral.no_results', 'No matches');
      listEl.appendChild(li);
      return;
    }
    const frag = document.createDocumentFragment();
    for (const e of entries) {
      const li = document.createElement('li');
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.className = 'ral-row';
      btn.setAttribute('role', 'option');
      btn.dataset.hex = e.hex;
      btn.innerHTML =
        '<span class="ral-row__swatch" style="background:' + e.hex + '"></span>' +
        '<span class="ral-row__code">' + e.key + '</span>' +
        '<span class="ral-row__names">' +
          '<span class="ral-row__nl"></span>' +
          '<span class="ral-row__en"></span>' +
        '</span>';
      btn.querySelector('.ral-row__nl').textContent = e.nl;
      btn.querySelector('.ral-row__en').textContent = e.en;
      btn.addEventListener('click', () => onPick(e.hex));
      li.appendChild(btn);
      frag.appendChild(li);
    }
    listEl.appendChild(frag);
  }

  function filter(entries, query) {
    const q = query.trim().toLowerCase();
    if (!q) return entries;
    return entries.filter((e) => e.searchIndex.indexOf(q) !== -1);
  }

  window.initRalPicker = function initRalPicker(opts) {
    const { openBtn, modal, closeBtn, search, list, onPick } = opts;
    if (!openBtn || !modal || !list) return;

    // The native <dialog> API gives us focus trap + Esc-to-close for free.
    // Fall back to plain display toggling on the rare ancient browser
    // (showModal landed in all evergreen browsers by 2022).
    const hasNativeDialog = typeof modal.showModal === 'function';

    function open() {
      loadEntries().then(
        (entries) => {
          render(list, entries, pickAndClose);
          if (search) {
            search.value = '';
            // Re-render on each keystroke. 205 rows is small enough that we
            // can rebuild the DOM rather than show/hide.
            search.oninput = () => render(list, filter(entries, search.value), pickAndClose);
          }
          if (hasNativeDialog) {
            modal.showModal();
          } else {
            modal.setAttribute('open', '');
          }
          if (search) setTimeout(() => search.focus(), 0);
        },
        (err) => {
          list.textContent = '';
          const li = document.createElement('li');
          li.className = 'ral-empty';
          li.textContent = T('dashboard.colour.ral.load_failed', 'Could not load RAL palette') +
                           ' (' + (err.message || err) + ')';
          list.appendChild(li);
          if (hasNativeDialog) modal.showModal(); else modal.setAttribute('open', '');
        }
      );
    }

    function close() {
      if (hasNativeDialog) modal.close(); else modal.removeAttribute('open');
    }

    function pickAndClose(hex) {
      try { onPick(hex); } finally { close(); }
    }

    openBtn.addEventListener('click', open);
    if (closeBtn) closeBtn.addEventListener('click', close);
    // Click on backdrop (the <dialog> itself, outside its content) closes too.
    modal.addEventListener('click', (e) => {
      if (e.target === modal) close();
    });
  };
})();
