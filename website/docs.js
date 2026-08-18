document.querySelectorAll('.code-block').forEach(block => {
  const button = block.querySelector('.copy-code');
  const code = block.querySelector('code');
  button?.addEventListener('click', async () => {
    await navigator.clipboard.writeText(code?.textContent || '');
    button.textContent = 'Copied';
    setTimeout(() => { button.textContent = 'Copy'; }, 1100);
  });
});
document.querySelectorAll('[data-doc-language]').forEach(link => link.addEventListener('click', () => {
  localStorage.setItem('vesta-language', link.dataset.docLanguage);
}));
