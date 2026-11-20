// the page translation library, run in an isolated world. it finds the texts, hands them out in reading order
// and puts translations back where they came from. the page cannot see its globals and what it keeps lasts
// between interface calls
(function () {
  if (self.yzTr)
    return;
  var SKIP = { SCRIPT: 1, STYLE: 1, NOSCRIPT: 1, CODE: 1, PRE: 1, KBD: 1, SAMP: 1, TEXTAREA: 1, INPUT: 1, SELECT: 1, OPTION: 1, SVG: 1, MATH: 1,
               IFRAME: 1, CANVAS: 1, TEMPLATE: 1, HEAD: 1, TITLE: 1, OBJECT: 1, EMBED: 1 };
  var LETTER = /[A-Za-zÀ-ɏͰ-ϿЀ-ԯ԰-֏֐-ۿݐ-ݿऀ-෿฀-๿Ⴀ-ჿᄀ-ᇿሀ-፿぀-ヿ㐀-鿿가-힯]/;
  var RTL = { ar: 1, he: 1, fa: 1, ur: 1 };
  var MAX_ITEMS = 6000, MAX_LENGTH = 3000;
  var st = { items: [], known: new WeakMap(), watching: false, dirty: false, observer: null, original: null, target: '', hidden: new Map() };

  function utf8(text) {
    return unescape(encodeURIComponent(text)).length;
  }

  // an element whose text the reader never sees translated: code, fields, what the page marks, what is hidden
  function blocked(element) {
    for (var e = element; e && e !== document.documentElement; e = e.parentElement) {
      if (SKIP[e.tagName] || SKIP[e.localName ? e.localName.toUpperCase() : ''])
        return true;
      if (e.isContentEditable || e.getAttribute('translate') === 'no' || (e.classList && e.classList.contains('notranslate')))
        return true;
      if (e.hidden || e.getAttribute('aria-hidden') === 'true')
        return true;
    }
    return false;
  }

  function hiddenElement(element) {
    var cached = st.hidden.get(element);
    if (cached !== undefined)
      return cached;
    var style = element.ownerDocument.defaultView.getComputedStyle(element);
    var hidden = !style || style.display === 'none' || style.visibility === 'hidden';
    st.hidden.set(element, hidden);
    return hidden;
  }

  function add(node, attribute, text) {
    var lead = text.match(/^\s*/)[0], trail = text.match(/\s*$/)[0];
    var core = text.trim().replace(/\s+/g, ' ');
    if (!core || core.length > MAX_LENGTH || !LETTER.test(core))
      return;
    var item = { node: node, attribute: attribute, original: text, lead: lead, trail: trail, text: core, current: null, done: false };
    st.items.push(item);
    if (!attribute)
      st.known.set(node, item);
  }

  function visibleFirst(items) {
    var height = (window.innerHeight || 800) * 1.6;
    var near = [], far = [];
    items.forEach(function (item) {
      var element = item.attribute ? item.node : item.node.parentElement;
      var rect = element && element.getBoundingClientRect ? element.getBoundingClientRect() : null;
      (rect && rect.bottom >= -150 && rect.top <= height ? near : far).push(item);
    });
    return near.concat(far);
  }

  // what is new since the last call: text nodes that no item holds, and nodes that the page rewrote after they were translated
  function collect() {
    st.hidden = new Map();
    var fresh = [];
    var before = st.items.length;
    var saved = st.items;
    st.items = fresh;
    var root = document.body;
    if (root) {
      var walker = document.createTreeWalker(root, NodeFilter.SHOW_TEXT, null);
      for (var node = walker.nextNode(); node && fresh.length + before < MAX_ITEMS; node = walker.nextNode()) {
        var known = st.known.get(node);
        if (known && known.current !== null && known.current === node.nodeValue)
          continue;
        if (known && known.current === null)
          continue;
        var parent = node.parentElement;
        if (!parent || !node.nodeValue || blocked(parent) || hiddenElement(parent))
          continue;
        add(node, null, node.nodeValue);
      }
      var list = root.querySelectorAll('[title],[alt],[placeholder]');
      for (var i = 0; i < list.length && fresh.length + before < MAX_ITEMS; ++i) {
        var element = list[i];
        // a field is blocked for its text but not for its placeholder and title, so only what holds it and what the page marks on it count
        if ((element.parentElement && blocked(element.parentElement)) || element.getAttribute('translate') === 'no' || element.classList.contains('notranslate'))
          continue;
        ['title', 'alt', 'placeholder'].forEach(function (name) {
          var value = element.getAttribute(name);
          if (!value)
            return;
          for (var j = 0; j < saved.length; ++j) {
            if (saved[j].node === element && saved[j].attribute === name && (saved[j].current === value || saved[j].current === null))
              return;
          }
          add(element, name, value);
        });
      }
    }
    st.items = saved;
    var ordered = visibleFirst(fresh);
    var from = st.items.length;
    ordered.forEach(function (item) { st.items.push(item); });
    return { from: from, lengths: ordered.map(function (item) { return utf8(item.text); }) };
  }

  function animate(item) {
    var element = item.attribute ? null : item.node.parentElement;
    if (!element || !element.animate)
      return;
    var rect = element.getBoundingClientRect();
    if (rect.bottom < 0 || rect.top > (window.innerHeight || 800))
      return;
    // the text settles past its place and comes back, the way the controls of the app do
    element.animate([{ opacity: 0.2, transform: 'translateY(6px) scale(0.98)' }, { opacity: 1, transform: 'none' }],
                    { duration: 380, easing: 'cubic-bezier(0.34, 1.56, 0.64, 1)' });
  }

  var animated = 0;

  function put(start, texts) {
    var still = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    var count = 0;
    for (var i = 0; i < texts.length; ++i) {
      var item = st.items[start + i];
      var text = texts[i];
      if (!item || typeof text !== 'string' || !text)
        continue;
      var value = item.lead + text + item.trail;
      if (item.attribute) {
        item.node.setAttribute(item.attribute, value);
        item.current = value;
      } else {
        // a node the page rewrote while the answer was on its way keeps the page's text
        if (item.node.nodeValue !== item.original)
          continue;
        item.node.nodeValue = value;
        item.current = value;
      }
      item.done = true;
      ++count;
      if (!still && animated < 80) {
        ++animated;
        animate(item);
      }
    }
    return count;
  }

  // what the page changed: new nodes and rewritten text, and not what put() wrote
  function handle(records) {
    for (var i = 0; i < records.length; ++i) {
      var record = records[i];
      if (record.type === 'characterData') {
        var item = st.known.get(record.target);
        if (item && item.current !== null && item.current === record.target.nodeValue)
          continue;
      }
      st.dirty = true;
      return;
    }
  }

  function watch(on, language) {
    st.target = language || '';
    if (on && !st.watching && window.MutationObserver) {
      st.observer = new MutationObserver(handle);
      st.observer.observe(document.documentElement, { childList: true, subtree: true, characterData: true });
      st.watching = true;
    }
    if (!on && st.observer) {
      st.observer.disconnect();
      st.observer = null;
      st.watching = false;
    }
    var root = document.documentElement;
    if (on && language) {
      if (st.original === null)
        st.original = { lang: root.getAttribute('lang'), dir: root.getAttribute('dir') };
      root.setAttribute('lang', language);
      if (RTL[language.split('-')[0]])
        root.setAttribute('dir', 'rtl');
    }
  }

  function restore() {
    watch(false, '');
    st.items.forEach(function (item) {
      if (!item.done || item === st.title)
        return;
      if (item.attribute) {
        if (item.node.getAttribute(item.attribute) === item.current)
          item.node.setAttribute(item.attribute, item.original);
      } else if (item.node.nodeValue === item.current) {
        item.node.nodeValue = item.original;
      }
    });
    if (st.original) {
      var root = document.documentElement;
      ['lang', 'dir'].forEach(function (name) {
        if (st.original[name] === null)
          root.removeAttribute(name);
        else
          root.setAttribute(name, st.original[name]);
      });
    }
    var title = st.title;
    if (title && document.title === title.current)
      document.title = title.original;
    st.items = [];
    st.known = new WeakMap();
    st.original = null;
    st.title = null;
    animated = 0;
  }

  self.yzTr = {
    // the language the page says and the start of what it shows, for the interface to name the language
    sample: function () {
      var text = document.body ? (document.body.innerText || '').slice(0, 4000) : '';
      return JSON.stringify({ lang: document.documentElement.getAttribute('lang') || '', title: document.title || '', text: text });
    },
    collect: function () {
      var found = collect();
      var title = document.title;
      if (title && !st.title && LETTER.test(title)) {
        // the title of the page is an item of its own, at the end
        st.title = { node: null, attribute: 'document.title', original: title, lead: '', trail: '', text: title.trim().replace(/\s+/g, ' '), current: null, done: false };
        st.items.push(st.title);
        found.lengths.push(utf8(st.title.text));
      }
      return JSON.stringify(found);
    },
    get: function (start, count) {
      var texts = [];
      for (var i = 0; i < count; ++i)
        texts.push(st.items[start + i] ? st.items[start + i].text : '');
      return JSON.stringify(texts);
    },
    put: function (start, json) {
      var texts = JSON.parse(json);
      var done = 0;
      for (var i = 0; i < texts.length; ++i) {
        var item = st.items[start + i];
        if (item && item === st.title && typeof texts[i] === 'string' && texts[i]) {
          document.title = texts[i];
          item.current = texts[i];
          item.done = true;
          texts[i] = '';
          ++done;
        }
      }
      return String(done + put(start, texts));
    },
    watch: function (on, language) { watch(on, language); return '1'; },
    dirty: function () {
      // the records the observer has not delivered yet count as well
      if (st.observer)
        handle(st.observer.takeRecords());
      var dirty = st.dirty;
      st.dirty = false;
      return dirty ? '1' : '0';
    },
    restore: function () { restore(); return '1'; }
  };
})();
