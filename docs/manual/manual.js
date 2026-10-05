const field = document.querySelector('#manual-search');
const chapters = [...document.querySelectorAll('main > section')];
const result = document.querySelector('#search-result');
function search() {
  const terms = field.value.trim().toLocaleLowerCase().split(/\s+/).filter(Boolean);
  let hits = 0;
  for (const chapter of chapters) {
    const cards = [...chapter.querySelectorAll('.command')];
    const matches = text => terms.every(term => text.toLocaleLowerCase().includes(term));
    if (cards.length) {
      for (const card of cards) card.classList.toggle('search-hidden', !matches(card.textContent));
      chapter.classList.toggle('search-hidden', cards.every(card => card.classList.contains('search-hidden')));
    } else chapter.classList.toggle('search-hidden', !matches(chapter.textContent));
    if (!chapter.classList.contains('search-hidden')) hits++;
  }
  result.textContent = terms.length ? `${hits} セクションが一致。検索解除で全項目に戻ります。` : '全項目を表示中';
}
field.addEventListener('input', search);
document.querySelector('#clear-search').addEventListener('click', () => {field.value=''; search(); field.focus();});
document.querySelector('#print-manual').addEventListener('click', () => window.print());
document.querySelectorAll('a[href^="#"]').forEach(link => link.addEventListener('click', () => {field.value=''; search();}));
search();
