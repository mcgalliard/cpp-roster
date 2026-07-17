"use strict";

// Roster Manager frontend. A single module-level `state` object holds everything
// the UI needs; render functions read from it. All user text is written through
// textContent / createElement — never innerHTML with data — so the UI cannot be
// used to inject markup.

const state = {
  people: [], // full roster from GET /api/people
  search: "", // current search box text (lowercased)
  activeOnly: false, // "Active only" toggle
  editingId: null, // id being edited, or null when adding
  importFile: null, // File chosen for import, pending a mode choice
};

// ---- DOM references --------------------------------------------------------

const els = {
  countBadge: document.getElementById("count-badge"),
  search: document.getElementById("search"),
  activeOnly: document.getElementById("active-only"),
  btnAdd: document.getElementById("btn-add"),
  btnImport: document.getElementById("btn-import"),
  btnExport: document.getElementById("btn-export"),
  body: document.getElementById("people-body"),
  emptyState: document.getElementById("empty-state"),
  modal: document.getElementById("modal"),
  modalTitle: document.getElementById("modal-title"),
  form: document.getElementById("person-form"),
  fFirst: document.getElementById("f-first"),
  fLast: document.getElementById("f-last"),
  fRole: document.getElementById("f-role"),
  fEmail: document.getElementById("f-email"),
  fActive: document.getElementById("f-active"),
  formError: document.getElementById("form-error"),
  formSubmit: document.getElementById("form-submit"),
  importModal: document.getElementById("import-modal"),
  importFilename: document.getElementById("import-filename"),
  importMerge: document.getElementById("import-merge"),
  importReplace: document.getElementById("import-replace"),
  importFileInput: document.getElementById("import-file"),
  toasts: document.getElementById("toasts"),
};

// ---- Small helpers ---------------------------------------------------------

// Create an element with a class and (optionally) text content, set safely.
function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined && text !== null) node.textContent = text;
  return node;
}

// Show a transient toast. `kind` is "success" or "error".
function toast(message, kind = "success") {
  const node = el("div", `toast toast--${kind}`, message);
  els.toasts.appendChild(node);
  setTimeout(() => {
    node.style.transition = "opacity 0.3s ease";
    node.style.opacity = "0";
    setTimeout(() => node.remove(), 300);
  }, 3200);
}

// Parse a JSON error body, returning its `error` field or a fallback message.
async function errorMessage(res, fallback) {
  try {
    const data = await res.json();
    if (data && typeof data.error === "string") return data.error;
  } catch (_) {
    /* body was not JSON; fall through */
  }
  return fallback;
}

// ---- Data loading ----------------------------------------------------------

async function loadPeople() {
  try {
    const res = await fetch("/api/people");
    if (!res.ok) throw new Error(await errorMessage(res, "Failed to load roster."));
    state.people = await res.json();
    renderTable();
  } catch (err) {
    toast(err.message || "Could not reach the server.", "error");
  }
}

// ---- Filtering & rendering -------------------------------------------------

// The people that match the current search + active-only filters, sorted by
// last name (then first name) to mirror the CLI's ordering.
function visiblePeople() {
  const q = state.search;
  return state.people
    .filter((p) => {
      if (state.activeOnly && !p.active) return false;
      if (!q) return true;
      const haystack = `${p.first} ${p.last} ${p.role} ${p.email}`.toLowerCase();
      return haystack.includes(q);
    })
    .sort((a, b) => {
      const byLast = a.last.localeCompare(b.last, undefined, { sensitivity: "base" });
      if (byLast !== 0) return byLast;
      return a.first.localeCompare(b.first, undefined, { sensitivity: "base" });
    });
}

function renderTable() {
  const rows = visiblePeople();

  els.countBadge.textContent =
    rows.length === state.people.length
      ? `${state.people.length} ${state.people.length === 1 ? "person" : "people"}`
      : `${rows.length} of ${state.people.length}`;

  els.body.textContent = "";

  if (rows.length === 0) {
    els.emptyState.hidden = false;
    els.emptyState.textContent =
      state.people.length === 0
        ? "No people yet. Click “Add Person” to get started."
        : "No people match your filters.";
    return;
  }
  els.emptyState.hidden = true;

  for (const p of rows) {
    els.body.appendChild(renderRow(p));
  }
}

function renderRow(p) {
  const tr = el("tr", p.active ? "" : "is-inactive");

  const nameTd = el("td");
  nameTd.appendChild(el("span", "cell-name", `${p.first} ${p.last}`.trim()));
  tr.appendChild(nameTd);

  tr.appendChild(el("td", p.role ? "" : "cell-muted", p.role || "—"));
  tr.appendChild(el("td", p.email ? "" : "cell-muted", p.email || "—"));

  const statusTd = el("td");
  statusTd.appendChild(
    el(
      "span",
      `pill ${p.active ? "pill--active" : "pill--inactive"}`,
      p.active ? "Active" : "Inactive"
    )
  );
  tr.appendChild(statusTd);

  const actionsTd = el("td", "col-actions");
  const wrap = el("div", "row-actions");
  const editBtn = el("button", "btn btn--icon", "Edit");
  editBtn.type = "button";
  editBtn.addEventListener("click", () => openEdit(p));
  const delBtn = el("button", "btn btn--icon btn--danger", "Delete");
  delBtn.type = "button";
  delBtn.addEventListener("click", () => deletePerson(p));
  wrap.appendChild(editBtn);
  wrap.appendChild(delBtn);
  actionsTd.appendChild(wrap);
  tr.appendChild(actionsTd);

  return tr;
}

// ---- Add / Edit modal ------------------------------------------------------

function openAdd() {
  state.editingId = null;
  els.modalTitle.textContent = "Add Person";
  els.form.reset();
  els.fActive.checked = true;
  clearFormErrors();
  showModal();
}

function openEdit(p) {
  state.editingId = p.id;
  els.modalTitle.textContent = "Edit Person";
  els.fFirst.value = p.first;
  els.fLast.value = p.last;
  els.fRole.value = p.role;
  els.fEmail.value = p.email;
  els.fActive.checked = p.active;
  clearFormErrors();
  showModal();
}

function showModal() {
  els.modal.hidden = false;
  els.fFirst.focus();
}

function closeModal() {
  els.modal.hidden = true;
}

function clearFormErrors() {
  els.formError.hidden = true;
  els.formError.textContent = "";
  els.fFirst.classList.remove("is-invalid");
  els.fLast.classList.remove("is-invalid");
}

function showFormError(message) {
  els.formError.textContent = message;
  els.formError.hidden = false;
}

// Submit handler for both add and edit. Client-side required validation on
// first/last; server 400/409 errors are shown inline without closing the modal.
async function submitForm(event) {
  event.preventDefault();
  clearFormErrors();

  const first = els.fFirst.value.trim();
  const last = els.fLast.value.trim();
  let invalid = false;
  if (!first) {
    els.fFirst.classList.add("is-invalid");
    invalid = true;
  }
  if (!last) {
    els.fLast.classList.add("is-invalid");
    invalid = true;
  }
  if (invalid) {
    showFormError("First and last name are required.");
    return;
  }

  const payload = {
    first,
    last,
    role: els.fRole.value.trim(),
    email: els.fEmail.value.trim(),
    active: els.fActive.checked,
  };

  const editing = state.editingId !== null;
  const url = editing ? `/api/people/${state.editingId}` : "/api/people";
  const method = editing ? "PUT" : "POST";

  els.formSubmit.disabled = true;
  try {
    const res = await fetch(url, {
      method,
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    });
    if (!res.ok) {
      showFormError(await errorMessage(res, "Could not save this person."));
      return;
    }
    closeModal();
    await loadPeople();
    toast(editing ? "Person updated." : "Person added.");
  } catch (err) {
    showFormError(err.message || "Could not reach the server.");
  } finally {
    els.formSubmit.disabled = false;
  }
}

// ---- Delete ----------------------------------------------------------------

async function deletePerson(p) {
  const name = `${p.first} ${p.last}`.trim();
  if (!window.confirm(`Delete ${name}? This cannot be undone.`)) return;
  try {
    const res = await fetch(`/api/people/${p.id}`, { method: "DELETE" });
    if (!res.ok && res.status !== 204) {
      throw new Error(await errorMessage(res, "Could not delete this person."));
    }
    await loadPeople();
    toast(`Deleted ${name}.`);
  } catch (err) {
    toast(err.message || "Could not reach the server.", "error");
  }
}

// ---- Export ----------------------------------------------------------------

function exportRoster() {
  // Navigate so the browser handles the file download (Content-Disposition).
  window.location.href = "/api/export";
}

// ---- Import ----------------------------------------------------------------

function pickImportFile() {
  els.importFileInput.value = ""; // allow re-selecting the same file
  els.importFileInput.click();
}

function onImportFileChosen() {
  const file = els.importFileInput.files[0];
  if (!file) return;
  state.importFile = file;
  els.importFilename.textContent = file.name;
  els.importModal.hidden = false;
}

function closeImportModal() {
  els.importModal.hidden = true;
  state.importFile = null;
}

async function runImport(mode) {
  const file = state.importFile;
  els.importModal.hidden = true;
  if (!file) return;
  try {
    const res = await fetch(`/api/import?mode=${mode}`, {
      method: "POST",
      headers: { "Content-Type": "application/octet-stream" },
      body: file,
    });
    if (!res.ok) {
      throw new Error(await errorMessage(res, "Import failed."));
    }
    const data = await res.json();
    const n = data.imported;
    await loadPeople();
    toast(`Imported ${n} ${n === 1 ? "person" : "people"}.`);
  } catch (err) {
    toast(err.message || "Could not import that file.", "error");
  } finally {
    state.importFile = null;
  }
}

// ---- Wiring ----------------------------------------------------------------

function init() {
  els.search.addEventListener("input", () => {
    state.search = els.search.value.trim().toLowerCase();
    renderTable();
  });
  els.activeOnly.addEventListener("change", () => {
    state.activeOnly = els.activeOnly.checked;
    renderTable();
  });

  els.btnAdd.addEventListener("click", openAdd);
  els.btnExport.addEventListener("click", exportRoster);
  els.btnImport.addEventListener("click", pickImportFile);
  els.importFileInput.addEventListener("change", onImportFileChosen);

  els.form.addEventListener("submit", submitForm);
  els.importMerge.addEventListener("click", () => runImport("merge"));
  els.importReplace.addEventListener("click", () => runImport("replace"));

  // Close buttons / backdrops for both modals.
  for (const node of document.querySelectorAll("[data-close]")) {
    node.addEventListener("click", closeModal);
  }
  for (const node of document.querySelectorAll("[data-close-import]")) {
    node.addEventListener("click", closeImportModal);
  }
  document.addEventListener("keydown", (e) => {
    if (e.key === "Escape") {
      closeModal();
      closeImportModal();
    }
  });

  loadPeople();
}

init();
