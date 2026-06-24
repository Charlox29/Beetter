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

let scrollY = 0;

function toggleMenu(open) {
    hamburger.classList.toggle('open', open);
    navLinks.classList.toggle('open', open);
    navScrim.classList.toggle('open', open);
    hamburger.setAttribute('aria-expanded', String(open));
    if (open) {
        scrollY = window.scrollY;
        document.body.classList.add('no-scroll');
        document.body.style.top = `-${scrollY}px`;
    } else {
        document.body.classList.remove('no-scroll');
        document.body.style.top = '';
        window.scrollTo(0, scrollY);
    }
}

hamburger.addEventListener('click', () => toggleMenu(!hamburger.classList.contains('open')));
navScrim.addEventListener('click', () => toggleMenu(false));
navLinks.querySelectorAll('a').forEach(a => {
    a.addEventListener('click', function(e) {
        const href = this.getAttribute('href');
        if (href && href.startsWith('#')) {
            // On empêche le saut natif du navigateur vers l'ancre : sans ça, le
            // navigateur déplace déjà la page pendant les 50ms d'attente
            // ci-dessous, et le calcul manuel ci-après se fait alors par-dessus
            // un défilement déjà en cours, ce qui peut désynchroniser l'arrivée.
            e.preventDefault();
            toggleMenu(false);
            setTimeout(() => {
                const target = document.querySelector(href);
                if (target) {
                    const navbarHeight = document.querySelector('.navbar')?.offsetHeight || 72;
                    const top = target.getBoundingClientRect().top + window.scrollY - navbarHeight;
                    window.scrollTo({ top, behavior: 'smooth' });
                }
            }, 50);
        } else {
            toggleMenu(false);
        }
    });
});

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

// === ANIMATION COMPTEUR DES STATS ===
function animateCounter(el) {
    const target   = parseFloat(el.dataset.target);
    const decimals = parseInt(el.dataset.decimals || 0);
    const suffix   = el.dataset.suffix || '';
    const prefix   = el.dataset.prefix || '';
    const format   = el.dataset.format || '';
    const duration = 1800;
    const start    = performance.now();

    function step(now) {
        const progress = Math.min((now - start) / duration, 1);
        // Easing ease-out cubic
        const eased = 1 - Math.pow(1 - progress, 3);
        const value = target * eased;

        let display;
        if (decimals > 0) {
            display = value.toFixed(decimals).replace('.', ',');
        } else if (format === 'space') {
            display = Math.round(value).toLocaleString('fr-FR').replace(/\u202f/g, '\u00a0');
        } else {
            display = Math.round(value).toString();
        }

        el.textContent = prefix + display + suffix;
        if (progress < 1) requestAnimationFrame(step);
    }
    requestAnimationFrame(step);
}

const statEls = document.querySelectorAll('.stat-value[data-target]');
if (statEls.length) {
    const statObserver = new IntersectionObserver((entries) => {
        entries.forEach(e => {
            if (e.isIntersecting) {
                animateCounter(e.target);
                statObserver.unobserve(e.target);
            }
        });
    }, { threshold: 0.5 });
    statEls.forEach(el => statObserver.observe(el));
}

// === SMOOTH SCROLL pour les ancres ===
// Le scroll-padding-top: 80px dans le CSS compense la navbar fixe.
// On laisse le navigateur gérer nativement via scroll-behavior: smooth (CSS).

// === INDICATEUR DE PROGRESSION DE SCROLL ===
// Fine barre fixée en haut de l'écran qui se remplit de 0 à 100% selon
// la position de lecture sur la page. Throttling via requestAnimationFrame
// pour ne pas recalculer à chaque event de scroll (plus fluide, moins coûteux).
const scrollProgressBar = document.getElementById('scroll-progress-bar');
if (scrollProgressBar) {
    let scrollTicking = false;

    function updateScrollProgress() {
        const scrollTop = window.scrollY;
        const docHeight = document.documentElement.scrollHeight - window.innerHeight;
        const progress = docHeight > 0 ? (scrollTop / docHeight) * 100 : 0;
        scrollProgressBar.style.width = Math.min(100, Math.max(0, progress)) + '%';
        scrollTicking = false;
    }

    window.addEventListener('scroll', () => {
        if (!scrollTicking) {
            requestAnimationFrame(updateScrollProgress);
            scrollTicking = true;
        }
    });

    // Recalcule aussi au resize (la hauteur totale de page peut changer,
    // ex. rotation d'écran ou contenu qui se redéploie).
    window.addEventListener('resize', updateScrollProgress);

    updateScrollProgress();
}

