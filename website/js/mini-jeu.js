/* ═══════════════════════════════════════════════════════════ */
/*  MINI-JEU.JS — logique du jeu "Chassez les frelons"           */
/*  Page concernée : mini-jeu.html                               */
/*                                                                */
/*  Règles : on gagne des points en éliminant les frelons (🐝     */
/*  teintés en rouge via filtre CSS), on en perd en touchant les  */
/*  abeilles amies. La partie se termine si un frelon atteint     */
/*  la ruche, si le score devient négatif, ou au bout de 60s.     */
/*                                                                */
/*  NB : la gestion du thème et de la langue est mutualisée      */
/*  dans theme-lang.js (à charger avant ce fichier).             */
/* ═══════════════════════════════════════════════════════════ */

// === ÉTAT DU JEU ===
let score = 0;
let gameInterval = null;
let beeInterval = null;
let collisionCheckInterval = null;
let gameActive = false;
let hornetsKilled = 0;
let beesTouched = 0;
let combo = 0;
let bestCombo = 0;
let currentWave = 1;
let gameStartTime = 0;
let timeRemaining = 60;
let timerInterval = null;
let waveInterval = null;
let hornetsPerSecond = 1000; // intervalle de spawn des frelons en ms
let beesPerSecond = 2000;    // intervalle de spawn des abeilles en ms

// === RÉFÉRENCES DOM ===
const scoreBox          = document.getElementById('score-box');
const scoreVal           = document.getElementById('score-val');
const beesContainer      = document.getElementById('bees-container');
const hornetsContainer   = document.getElementById('hornets-container');
const startBtnFr         = document.getElementById('start-game-btn');
const startBtnEn         = document.getElementById('start-game-btn-en');
const gameOverEl         = document.getElementById('game-over');
const finalScoreEl       = document.getElementById('final-score');
const restartBtn         = document.getElementById('restart-btn');
const gameLegend         = document.getElementById('game-legend');
const hiveGraphic        = document.getElementById('hive-graphic');
const gameHud            = document.getElementById('game-hud');
const timerDisplay       = document.getElementById('timer');
const waveDisplay        = document.getElementById('wave-badge');
const comboDisplay       = document.getElementById('combo-display');

// === LOCALSTORAGE (record personnel) ===
function saveStats() {
    const stats = {
        bestScore: Math.max(score, parseInt(localStorage.getItem('bestScore') || 0)),
        lastWave: currentWave,
        plays: (parseInt(localStorage.getItem('plays') || 0)) + 1
    };
    localStorage.setItem('bestScore', stats.bestScore);
    localStorage.setItem('lastWave', stats.lastWave);
    localStorage.setItem('plays', stats.plays);
}

function getBestRecord() {
    return parseInt(localStorage.getItem('bestScore') || 0);
}

// === UTILITAIRES GÉOMÉTRIE / COLLISION ===
function getHiveRect() {
    return hiveGraphic.getBoundingClientRect();
}

function rectsOverlap(r1, r2) {
    return !(r1.right < r2.left || r1.left > r2.right || r1.bottom < r2.top || r1.top > r2.bottom);
}

// === FEEDBACK VISUEL ===
function showPopup(x, y, text, color) {
    const pop = document.createElement('div');
    pop.className = 'score-popup';
    pop.textContent = text;
    pop.style.left = x + 'px';
    pop.style.top  = y + 'px';
    pop.style.color = color;
    document.body.appendChild(pop);
    setTimeout(() => pop.remove(), 800);
}

// Affiche le badge "Vague N" avec une petite animation pop.
function showWaveBadge() {
    waveDisplay.textContent = `Vague ${currentWave}`;
    waveDisplay.style.animation = 'none';
    setTimeout(() => {
        waveDisplay.style.animation = 'wavePopup 0.5s ease-out';
    }, 50);
}

// Augmente la difficulté toutes les 10 secondes (vitesse + fréquence de spawn).
function updateWave() {
    const elapsed = 60 - timeRemaining;
    const newWave = Math.floor(elapsed / 10) + 1;
    if (newWave > currentWave) {
        currentWave = newWave;
        hornetsPerSecond = Math.max(500, 1500 - (currentWave - 1) * 150);  // min 500ms
        beesPerSecond = Math.max(1500, 3000 - (currentWave - 1) * 200);    // min 1500ms
        showWaveBadge();
        // Relance les intervalles avec les nouveaux délais
        clearInterval(gameInterval);
        clearInterval(beeInterval);
        gameInterval = setInterval(spawnHornet, hornetsPerSecond);
        beeInterval = setInterval(spawnBee, beesPerSecond);
    }
}

function updateTimer() {
    timeRemaining--;
    timerDisplay.textContent = timeRemaining + 's';

    if (timeRemaining <= 0) {
        endGame('timeout');
        return;
    }

    updateWave();
}

function updateCombo(increased) {
    if (increased) {
        combo++;
        bestCombo = Math.max(bestCombo, combo);
    } else {
        combo = 0;
    }
    comboDisplay.textContent = `Combo: ${combo}x +${combo + 1}`;
}

// === SPAWN DES ENTITÉS ===

// Frelon : traverse l'écran d'un bord à l'autre. Le joueur doit l'éliminer
// avant qu'il n'atteigne la ruche (sinon : game over).
function spawnHornet() {
    if (!gameActive) return;
    const hornet = document.createElement('div');
    hornet.classList.add('hornet');
    hornet.textContent = '🐝';

    const w = window.innerWidth;
    const h = window.innerHeight;
    let startX, startY, endX, endY;
    const edge = Math.floor(Math.random() * 4);
    switch (edge) {
        case 0: startX = Math.random()*w; startY = -100; endX = Math.random()*w; endY = h+100; break;
        case 1: startX = w+100; startY = Math.random()*h; endX = -100; endY = Math.random()*h; break;
        case 2: startX = Math.random()*w; startY = h+100; endX = Math.random()*w; endY = -100; break;
        default: startX = -100; startY = Math.random()*h; endX = w+100; endY = Math.random()*h;
    }

    const isRight = endX > startX;
    const scale = isRight ? 'scaleX(-1)' : 'scaleX(1)';
    const duration = Math.random()*4000 + 4000;

    function handleHornetKill(e) {
        if (!gameActive) return;
        e.preventDefault && e.preventDefault();
        const clientX = (e.touches && e.touches[0]) ? e.touches[0].clientX : e.clientX;
        const clientY = (e.touches && e.touches[0]) ? e.touches[0].clientY : e.clientY;

        // Points bonifiés par le combo en cours
        const points = combo + 1;
        score += points;
        hornetsKilled++;
        updateCombo(true);

        scoreVal.textContent = score;
        const hiveScoreEl = document.getElementById('hive-score-val');
        if (hiveScoreEl) hiveScoreEl.textContent = score;
        showPopup(clientX, clientY, `+${points} 🎯`, '#4caf50');
        this.remove();
    }
    hornet.addEventListener('mousedown', handleHornetKill);
    hornet.addEventListener('touchstart', handleHornetKill, { passive: false });

    hornetsContainer.appendChild(hornet);

    const anim = hornet.animate([
        { transform: `translate(${startX}px, ${startY}px) ${scale}` },
        { transform: `translate(${endX}px, ${endY}px) ${scale}` }
    ], { duration, easing: 'linear' });

    // Vérifie la collision avec la ruche à chaque frame, tant que le frelon est vivant.
    let hornetAlive = true;
    function checkHiveCollision() {
        if (!gameActive || !hornetAlive) return;
        if (!document.body.contains(hornet)) return;
        const hornetRect = hornet.getBoundingClientRect();
        const hiveRect = getHiveRect();
        if (rectsOverlap(hornetRect, hiveRect)) {
            hornetAlive = false;
            hornet.remove();
            endGame('hive');
            return;
        }
        requestAnimationFrame(checkHiveCollision);
    }
    requestAnimationFrame(checkHiveCollision);

    anim.onfinish = () => { hornetAlive = false; if (document.body.contains(hornet)) hornet.remove(); };
}

// Abeille amie : à éviter. Va-et-vient continu entre deux points aléatoires.
function spawnBee() {
    if (!gameActive) return;
    const bee = document.createElement('div');
    bee.classList.add('bee');
    bee.textContent = '🐝';

    const w = window.innerWidth;
    const h = window.innerHeight;
    const startX = Math.random()*(w-80);
    const startY = Math.random()*(h-80);
    const endX   = Math.random()*(w-80);
    const endY   = Math.random()*(h-80);
    const duration = Math.random()*4000 + 5000;

    bee.style.left = `${startX}px`;
    bee.style.top  = `${startY}px`;

    function handleBeeHit(e) {
        if (!gameActive) return;
        e.preventDefault && e.preventDefault();
        const clientX = (e.touches && e.touches[0]) ? e.touches[0].clientX : e.clientX;
        const clientY = (e.touches && e.touches[0]) ? e.touches[0].clientY : e.clientY;
        score--;
        beesTouched++;
        updateCombo(false); // Réinitialise le combo
        scoreVal.textContent = score;
        const hiveScoreEl = document.getElementById('hive-score-val');
        if (hiveScoreEl) hiveScoreEl.textContent = score;
        showPopup(clientX, clientY, '-1 😢', '#ff9800');
        this.remove();
        if (score < 0) {
            endGame('bee');
        }
    }
    bee.addEventListener('mousedown', handleBeeHit);
    bee.addEventListener('touchstart', handleBeeHit, { passive: false });

    beesContainer.appendChild(bee);
    bee.animate([
        { transform: 'translate(0px, 0px)' },
        { transform: `translate(${endX-startX}px, ${endY-startY}px)` }
    ], { duration, easing: 'ease-in-out', direction: 'alternate', iterations: Infinity });
}

// === CYCLE DE VIE DE LA PARTIE ===

function startGame() {
    if (gameInterval) return;
    gameActive = true;
    score = 0;
    hornetsKilled = 0;
    beesTouched = 0;
    combo = 0;
    bestCombo = 0;
    currentWave = 1;
    timeRemaining = 60;
    hornetsPerSecond = 1500;
    beesPerSecond = 3000;

    scoreVal.textContent = 0;
    scoreBox.classList.add('active');
    startBtnFr.classList.add('is-hidden');
    startBtnEn.classList.add('is-hidden');
    gameLegend.classList.add('is-hidden');
    gameHud.classList.remove('is-hidden');

    const hiveScoreEl = document.getElementById('hive-score-val');
    if (hiveScoreEl) hiveScoreEl.textContent = '0';

    // Affiche la vague initiale
    showWaveBadge();
    updateCombo(false);
    timerDisplay.textContent = '60s';

    // Spawn de 6 abeilles au démarrage
    for (let i = 0; i < 6; i++) spawnBee();

    gameInterval = setInterval(spawnHornet, hornetsPerSecond);
    beeInterval = setInterval(spawnBee, beesPerSecond);
    timerInterval = setInterval(updateTimer, 1000);
}

function endGame(reason) {
    gameActive = false;
    clearInterval(gameInterval);
    clearInterval(beeInterval);
    clearInterval(timerInterval);
    gameInterval = null;
    beeInterval = null;
    timerInterval = null;
    beesContainer.innerHTML = '';
    hornetsContainer.innerHTML = '';
    gameHud.classList.add('is-hidden');

    // Temps survécu
    const timeSurvived = 60 - timeRemaining;

    // Résultat principal
    finalScoreEl.textContent = hornetsKilled;

    // Stats détaillées
    document.getElementById('stat-best-combo').textContent = bestCombo;
    document.getElementById('stat-wave').textContent = currentWave;
    document.getElementById('stat-bees-hit').textContent = beesTouched;
    document.getElementById('stat-time').textContent = timeSurvived + 's';

    // Record personnel
    const previousBestScore = getBestRecord();
    const isNewRecord = hornetsKilled > previousBestScore;
    document.getElementById('record-banner').classList.toggle('is-hidden', !isNewRecord);

    saveStats();

    // Message de fin de partie adapté à la cause de défaite
    const titleFr = gameOverEl.querySelector('h2.lang-fr');
    const titleEn = gameOverEl.querySelector('h2.lang-en');
    const subFr = gameOverEl.querySelector('p.lang-fr');
    const subEn = gameOverEl.querySelector('p.lang-en');
    if (reason === 'hive') {
        if (titleFr) titleFr.textContent = '🐝 La ruche est envahie !';
        if (titleEn) titleEn.textContent = '🐝 The hive is invaded!';
        if (subFr) subFr.textContent = 'frelons éliminés avant l\'invasion';
        if (subEn) subEn.textContent = 'hornets eliminated before invasion';
    } else if (reason === 'bee') {
        if (titleFr) titleFr.textContent = '🐝 Score négatif !';
        if (titleEn) titleEn.textContent = '🐝 Negative score!';
        if (subFr) subFr.textContent = 'vous avez touché trop d\'abeilles';
        if (subEn) subEn.textContent = 'you hit too many bees';
    } else if (reason === 'timeout') {
        if (titleFr) titleFr.textContent = '⏰ Temps écoulé !';
        if (titleEn) titleEn.textContent = '⏰ Time\'s up!';
        if (subFr) subFr.textContent = 'frelons éliminés';
        if (subEn) subEn.textContent = 'hornets eliminated';
    }

    gameOverEl.style.display = 'flex';
    scoreBox.classList.remove('active');
}

function resetGame() {
    gameActive = false;
    clearInterval(gameInterval);
    clearInterval(beeInterval);
    clearInterval(timerInterval);
    gameInterval = null;
    beeInterval = null;
    timerInterval = null;
    beesContainer.innerHTML = '';
    hornetsContainer.innerHTML = '';
    gameOverEl.style.display = 'none';
    gameHud.classList.add('is-hidden');
    startBtnFr.classList.remove('is-hidden');
    startBtnEn.classList.remove('is-hidden');
    scoreBox.classList.remove('active');
    gameLegend.classList.remove('is-hidden');
    score = 0;
    scoreVal.textContent = 0;
    const hiveScoreEl = document.getElementById('hive-score-val');
    if (hiveScoreEl) hiveScoreEl.textContent = '0';
}

// === ÉCOUTEURS D'ÉVÉNEMENTS ===
startBtnFr.addEventListener('click', startGame);
startBtnEn.addEventListener('click', startGame);
restartBtn.addEventListener('click', resetGame);
