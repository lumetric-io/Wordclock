// Chronolett v2 i18n — small, dependency-free.
// Marks up: data-i18n="key" → element.textContent
//           data-i18n-placeholder="key" → element.placeholder
//           data-i18n-title="key" → element.title
// Dynamic strings (set from JS): use window.chronolettI18n.t('key', fallback).
//
// Language picked in this order:
//   1. ?lang=nl in the URL (also persisted)
//   2. localStorage('chronolett.lang')
//   3. navigator.language → 'nl' if it starts with nl, else 'en'
//
// fs is replaced on every UI OTA so this file ships alongside chronolett.css.
(function () {
  'use strict';

  var STORAGE_KEY = 'chronolett.lang';
  var SUPPORTED   = ['en', 'nl'];
  var DEFAULT     = 'en';

  var dict = {};            // current dictionary
  var currentLang = null;

  function detectInitial() {
    try {
      var url = new URL(window.location.href);
      var qp = url.searchParams.get('lang');
      if (qp && SUPPORTED.indexOf(qp) !== -1) {
        try { localStorage.setItem(STORAGE_KEY, qp); } catch (e) {}
        return qp;
      }
    } catch (e) {}
    try {
      var saved = localStorage.getItem(STORAGE_KEY);
      if (saved && SUPPORTED.indexOf(saved) !== -1) return saved;
    } catch (e) {}
    var nav = (navigator.language || '').toLowerCase();
    if (nav.indexOf('nl') === 0) return 'nl';
    return DEFAULT;
  }

  function t(key, fallback) {
    if (key in dict) return dict[key];
    return fallback != null ? fallback : key;
  }

  function applyToDom() {
    var nodes;
    nodes = document.querySelectorAll('[data-i18n]');
    for (var i = 0; i < nodes.length; i++) {
      var k = nodes[i].getAttribute('data-i18n');
      if (k in dict) nodes[i].textContent = dict[k];
    }
    nodes = document.querySelectorAll('[data-i18n-placeholder]');
    for (var i = 0; i < nodes.length; i++) {
      var k = nodes[i].getAttribute('data-i18n-placeholder');
      if (k in dict) nodes[i].setAttribute('placeholder', dict[k]);
    }
    nodes = document.querySelectorAll('[data-i18n-title]');
    for (var i = 0; i < nodes.length; i++) {
      var k = nodes[i].getAttribute('data-i18n-title');
      if (k in dict) nodes[i].setAttribute('title', dict[k]);
    }
    document.documentElement.lang = currentLang || DEFAULT;
    syncToggleUi();
  }

  function syncToggleUi() {
    var btns = document.querySelectorAll('[data-lang-toggle]');
    for (var i = 0; i < btns.length; i++) {
      var on = btns[i].getAttribute('data-lang-toggle') === currentLang;
      btns[i].classList.toggle('active', on);
      btns[i].setAttribute('aria-pressed', on ? 'true' : 'false');
    }
  }

  function load(lang) {
    return fetch('/i18n/' + lang + '.json', { cache: 'no-store' })
      .then(function (r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.json(); });
  }

  function setLanguage(lang) {
    if (SUPPORTED.indexOf(lang) === -1) lang = DEFAULT;
    return load(lang)
      .then(function (j) {
        dict = j || {};
        currentLang = lang;
        try { localStorage.setItem(STORAGE_KEY, lang); } catch (e) {}
        applyToDom();
        // Notify pages that re-render dynamic strings on language change.
        try {
          window.dispatchEvent(new CustomEvent('chronolett:lang', { detail: { lang: lang } }));
        } catch (e) {}
      })
      .catch(function (err) {
        // Soft-fail: leave the page untranslated rather than blank-replace.
        if (window.console && console.error) console.error('i18n load failed', err);
      });
  }

  function init() {
    var initial = detectInitial();
    currentLang = initial;
    setLanguage(initial);
    var btns = document.querySelectorAll('[data-lang-toggle]');
    for (var i = 0; i < btns.length; i++) {
      btns[i].addEventListener('click', function (e) {
        e.preventDefault();
        setLanguage(this.getAttribute('data-lang-toggle'));
      });
    }
  }

  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }

  // Public API
  window.chronolettI18n = {
    t: t,
    setLanguage: setLanguage,
    getLanguage: function () { return currentLang; },
  };
})();
