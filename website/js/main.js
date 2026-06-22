/* ═══════════════════════════════════════════════════════════ */
/*  MAIN.JS — comportements spécifiques à index.html             */
/*    - Menu hamburger (navigation mobile)                       */
/*    - Animation fade-in des blocs au scroll (IntersectionObserver) */
/*    - Smooth scroll pour les liens d'ancrage (#section)        */
/*                                                                */
/*  NB : la gestion du thème et de la langue est mutualisée      */
/*  dans theme-lang.js (à charger avant ce fichier).             */
/* ═══════════════════════════════════════════════════════════ */

// === HAMBURGER MENU ===
const hamburger = document.getElementById('hamburger');
const navLinks  = document.getElementById('nav-links');
const navScrim  = document.getElementById('nav-scrim');

function toggleMenu(open) {
    hamburger.classList.toggle('open', open);
    navLinks.classList.toggle('open', open);
    navScrim.classList.toggle('open', open);
    hamburger.setAttribute('aria-expanded', String(open));
    document.body.classList.toggle('no-scroll', open);
}

hamburger.addEventListener('click', () => toggleMenu(!hamburger.classList.contains('open')));
navScrim.addEventListener('click', () => toggleMenu(false));
navLinks.querySelectorAll('a').forEach(a => a.addEventListener('click', () => toggleMenu(false)));

// === INTERSECTION OBSERVER (scroll fade-in) ===
// Fait apparaître en fondu les cartes/blocs au fur et à mesure du scroll,
// sauf si l'utilisateur a activé "réduire les animations" dans son OS.
const animTargets = document.querySelectorAll('.card, .feature-box, .tech-item, .team-card, .faq-item, .flow-step');
if (window.matchMedia('(prefers-reduced-motion: no-preference)').matches) {
    const io = new IntersectionObserver((entries) => {
        entries.forEach((e, i) => {
            if (e.isIntersecting) {
                e.target.style.transitionDelay = (i % 4) * 80 + 'ms';
                e.target.classList.add('visible');
                io.unobserve(e.target);
            }
        });
    }, { threshold: 0.12 });
    animTargets.forEach(el => io.observe(el));
} else {
    animTargets.forEach(el => el.classList.add('visible'));
}

// === SMOOTH SCROLL pour les ancres ===
document.querySelectorAll('a[href^="#"]').forEach(anchor => {
    anchor.addEventListener('click', function (e) {
        const target = document.querySelector(this.getAttribute('href'));
        if (target) {
            e.preventDefault();
            target.scrollIntoView({ behavior: 'smooth', block: 'start' });
        }
    });
});
