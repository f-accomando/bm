/*
 * bm Studio <-> bm Animator: the project passes from one page to the other
 * (the bytes of the .bm, its name and, in Chrome and Edge, the file itself so
 * that Save still writes it where it is). Kept in IndexedDB for a minute.
 */
(function (root) {
  'use strict';
  const BM = root.BM;
  const DB = 'bm-sdk', STORE = 'handoff', KEY = 'project';

  function open() {
    return new Promise((resolve, reject) => {
      if (!root.indexedDB) { reject(new Error('no IndexedDB')); return; }
      const rq = root.indexedDB.open(DB, 1);
      rq.onupgradeneeded = () => rq.result.createObjectStore(STORE);
      rq.onsuccess = () => resolve(rq.result);
      rq.onerror = () => reject(rq.error);
    });
  }

  async function put(data) {
    const db = await open();
    await new Promise((resolve, reject) => {
      const tx = db.transaction(STORE, 'readwrite');
      tx.objectStore(STORE).put({ ...data, when: Date.now() }, KEY);
      tx.oncomplete = resolve;
      tx.onerror = () => reject(tx.error);
    });
    db.close();
  }

  /* what the other page left, if it is recent; it is taken away */
  async function take() {
    let db;
    try { db = await open(); } catch (e) { return null; }
    const data = await new Promise(resolve => {
      const tx = db.transaction(STORE, 'readwrite'), st = tx.objectStore(STORE), rq = st.get(KEY);
      rq.onsuccess = () => { st.delete(KEY); resolve(rq.result || null); };
      rq.onerror = () => resolve(null);
    });
    db.close();
    return data && Date.now() - data.when < 60000 ? data : null;
  }

  /* leaves the project and opens the other page */
  async function go(url, data) {
    try { await put(data); } catch (e) { /* the other page just opens empty */ }
    root.location.href = url;
  }

  BM.handoff = { put, take, go };
})(typeof window !== 'undefined' ? window : globalThis);
