/* ═══════════════════════════════════════════════════════════ */
/*  THEME-LANG.JS                                                */
/*  Logique partagée par TOUTES les pages du site Beetter :      */
/*    - Bascule thème clair / sombre (bouton soleil/lune)        */
/*    - Bascule langue FR / EN (bouton "FR | EN")                */
/*                                                                */
/*  Requiert dans le HTML les éléments suivants :                */
/*    #theme-toggle, #sun-icon, #moon-icon, #lang-toggle          */
/* ═══════════════════════════════════════════════════════════ */

// === THÈME ===
const themeBtn = document.getElementById('theme-toggle');
const sunIcon  = document.getElementById('sun-icon');
const moonIcon = document.getElementById('moon-icon');
const html     = document.documentElement;

// Thème sombre par défaut au chargement de la page.
html.setAttribute('data-theme', 'dark');
sunIcon.classList.remove('is-hidden');
moonIcon.classList.add('is-hidden');

themeBtn.addEventListener('click', () => {
    const newTheme = html.getAttribute('data-theme') === 'light' ? 'dark' : 'light';
    html.setAttribute('data-theme', newTheme);
    sunIcon.classList.toggle('is-hidden', newTheme !== 'dark');
    moonIcon.classList.toggle('is-hidden', newTheme === 'dark');
});

// === LANGUE ===
const langBtn = document.getElementById('lang-toggle');
langBtn.addEventListener('click', () => {
    const newLang = html.getAttribute('lang') === 'fr' ? 'en' : 'fr';
    html.setAttribute('lang', newLang);
    langBtn.textContent = newLang === 'fr' ? 'FR | EN' : 'EN | FR';
});
