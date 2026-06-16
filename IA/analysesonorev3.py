import numpy as np
import librosa
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
import random
import time
import joblib
import numpy as np
from influxdb_client import InfluxDBClient
from app.blueprints.utils.influxdb import write_ia_prediction
import os

# ==========================================
# 🛠️ FONCTIONS UTILITAIRES (Pour un code propre)
# ==========================================

def extraire_mfcc(audio, sr):
    """Calcule les 13 MFCC et fait la moyenne instantanément"""
    mfcc = librosa.feature.mfcc(y=audio, sr=sr, n_mfcc=13, fmin=50, fmax=2000)
    return np.mean(mfcc, axis=1)

def ajouter_bruit(audio, niveau=0.005):
    """Ajoute un léger bruit blanc aléatoire (type vent)"""
    bruit = np.random.randn(len(audio)) * niveau
    return audio + bruit

# ==========================================
# 🐝 PARTIE 1 : DÉTECTION DES FRELONS
# ==========================================
print("\n--- Entraînement du Modèle Frelon ---")

ruche, sr = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/ruche_exterieur.wav", sr=8000)
frelon1, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/frelon1.wav", sr=8000)
frelon2, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/frelon2.wav", sr=8000)
frelon3, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/frelon3.wav", sr=8000)
frelon4, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/frelon4.wav", sr=8000)

frelon = np.concatenate((frelon1, frelon2, frelon3, frelon4))

taille_fenetre = 3 * sr  # 3 secondes
saut = 3 * sr            # Pas de chevauchement !

blocs_ruche = librosa.util.frame(ruche, frame_length=taille_fenetre, hop_length=saut)
blocs_frelon = librosa.util.frame(frelon, frame_length=taille_fenetre, hop_length=saut)

nb_blocs = min(blocs_ruche.shape[1], blocs_frelon.shape[1])
X, Y = [], []

# 1. Traitement de la Ruche (Augmentation x2 : Original + Bruité)
for i in range(nb_blocs):
    morceau = blocs_ruche[:, i]
    X.append(extraire_mfcc(morceau, sr))
    X.append(extraire_mfcc(ajouter_bruit(morceau), sr)) # Clone bruité
    Y.extend([0, 0])

# 2. Traitement du Frelon (Augmentation x3 avec Mixage)
for i in range(nb_blocs):
    morceau_frelon = blocs_frelon[:, i]
    
    # On crée 3 clones avec le frelon plus ou moins loin du micro
    for clone in range(3): 
        idx_hasard = random.randint(0, blocs_ruche.shape[1] - 1)
        morceau_ruche = blocs_ruche[:, idx_hasard]
        vol_f = random.uniform(0.2, 1.5)
        
        mixage = (morceau_ruche * 0.5) + (morceau_frelon * vol_f)
        X.append(extraire_mfcc(mixage, sr))
        Y.append(1)

X = np.array(X)
Y = np.array(Y)

print(f"📊 Dataset Frelon : {len(X)} échantillons (Augmenté !)")
X_train, X_test, Y_train, Y_test = train_test_split(X, Y, test_size=0.2, random_state=42)

modele_f = RandomForestClassifier(n_estimators=100, random_state=42)
modele_f.fit(X_train, Y_train) 
print(f"🎯 Précision Frelon : {modele_f.score(X_test, Y_test) * 100:.2f}%")
# ==========================================
# 🌪️ PARTIE 2 : DÉTECTION DES ESSAIMAGES (CORRIGÉE)
# ==========================================
print("\n--- Entraînement du Modèle Essaimage ---")

essaimage1, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/essaimage1.wav", sr=8000)
essaimage2, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/essaimage2.wav", sr=8000)
essaimage = np.concatenate((essaimage1, essaimage2))

taille_fenetre_e = 5 * sr  # 5 secondes
saut_e = 5 * sr            

blocs_ruche_5s = librosa.util.frame(ruche, frame_length=taille_fenetre_e, hop_length=saut_e).T
blocs_essaimage = librosa.util.frame(essaimage, frame_length=taille_fenetre_e, hop_length=saut_e).T

# 1. ON FAIT LE SPLIT AVANT TOUTE AUGMENTATION (Garantie d'honnêteté)
r_train, r_test = train_test_split(blocs_ruche_5s, test_size=0.2, random_state=42)
e_train, e_test = train_test_split(blocs_essaimage, test_size=0.2, random_state=42)

A_train, B_train = [], []
A_test, B_test = [], []

# 2. ON FABRIQUE L'ENTRAÎNEMENT (Ici on a le droit d'augmenter/cloner !)
for morceau in r_train:
    A_train.append(extraire_mfcc(morceau, sr))
    A_train.append(extraire_mfcc(ajouter_bruit(morceau), sr)) # Clone autorisé
    B_train.extend([0, 0])

for morceau in e_train:
    A_train.append(extraire_mfcc(morceau, sr))
    A_train.append(extraire_mfcc(ajouter_bruit(morceau, niveau=0.01), sr)) # Clone autorisé
    B_train.extend([1, 1])

# 3. ON FABRIQUE LE TEST (Aucun clone, pure réalité)
for morceau in r_test:
    A_test.append(extraire_mfcc(morceau, sr))
    B_test.append(0)

for morceau in e_test:
    A_test.append(extraire_mfcc(morceau, sr))
    B_test.append(1)

A_train, B_train = np.array(A_train), np.array(B_train)
A_test, B_test = np.array(A_test), np.array(B_test)

print(f"📊 Dataset Essaimage : {len(A_train)} Train, {len(A_test)} Test")

modele_e = RandomForestClassifier(n_estimators=100, random_state=42)
modele_e.fit(A_train, B_train) 
print(f"🎯 VRAIE Précision Essaimage : {modele_e.score(A_test, B_test) * 100:.2f}%")
# ==========================================
# 👑 PARTIE 3 : CHANT DE LA REINE (CORRIGÉE)
# ==========================================
print("\n--- Entraînement du Modèle Reine ---")

# 1. Chargement des sons
ruche_int1, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/ruche_interieur1.wav", sr=8000)
ruche_int2, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/ruche_interieur2.wav", sr=8000)
ruche_int = np.concatenate((ruche_int1, ruche_int2))

fichiers_chant = ["chant2.wav", "chant3.wav", "chant4.wav", "chant5.wav", "chant7.wav", "chant8.wav", "chant9.wav", "chant10.wav", "chant11.wav"]
chant = np.array([])
for f in fichiers_chant:
    audio, _ = librosa.load(f"C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/{f}", sr=8000)
    chant = np.concatenate((chant, audio))

taille_fenetre_c = 3 * sr 
saut_c = 3 * sr            

# Astuce : Le ".T" (Transposition) à la fin permet de retourner le tableau 
# pour que train_test_split puisse le couper correctement.
blocs_ruche_int = librosa.util.frame(ruche_int, frame_length=taille_fenetre_c, hop_length=saut_c).T
blocs_chant = librosa.util.frame(chant, frame_length=taille_fenetre_c, hop_length=saut_c).T

# 2. ON SÉPARE AVANT D'AUGMENTER (Anti-Triche)
r_train, r_test = train_test_split(blocs_ruche_int, test_size=0.2, random_state=42)
c_train, c_test = train_test_split(blocs_chant, test_size=0.2, random_state=42)

C_train, D_train = [], []
C_test, D_test = [], []

# 3. CRÉATION DU DOSSIER ENTRAÎNEMENT (Avec Clones et Mixages)
# --> Ruche saine (Original + Clone bruité)
for morceau in r_train:
    C_train.append(extraire_mfcc(morceau, sr))
    C_train.append(extraire_mfcc(ajouter_bruit(morceau), sr)) 
    D_train.extend([0, 0])

# --> Chant de la reine (Original + 3 mixages aléatoires)
for morceau_c in c_train:
    # On garde le chant pur
    C_train.append(extraire_mfcc(morceau_c, sr))
    D_train.append(1)
    
    # On crée 3 mixages différents pour l'entraînement
    for clone in range(3):
        # On choisit un bruit de fond au hasard dans le dossier d'entraînement !
        idx_hasard = random.randint(0, len(r_train) - 1)
        fond_ruche = r_train[idx_hasard]
        vol_c = random.uniform(0.3, 1.5)
        
        mixage = (fond_ruche * 0.5) + (morceau_c * vol_c)
        C_train.append(extraire_mfcc(mixage, sr))
        D_train.append(1)

# 4. CRÉATION DU DOSSIER TEST (Pure Réalité, Zéro Tricherie)
for morceau in r_test:
    C_test.append(extraire_mfcc(morceau, sr))
    D_test.append(0)

for morceau in c_test:
    C_test.append(extraire_mfcc(morceau, sr))
    D_test.append(1)

# 5. ENTRAÎNEMENT ET VERDICT
C_train, D_train = np.array(C_train), np.array(D_train)
C_test, D_test = np.array(C_test), np.array(D_test)

print(f"📊 Dataset Reine : {len(C_train)} Train, {len(C_test)} Test")

modele_c = RandomForestClassifier(n_estimators=100, random_state=42)
modele_c.fit(C_train, D_train) 
print(f"🎯 VRAIE Précision Reine : {modele_c.score(C_test, D_test) * 100:.2f}%")

modele_e = joblib.load("modele_essaimage.pkl")
joblib.dump(modele_f, 'models/detector_frelon.pkl')
joblib.dump(modele_c, 'models/detector_reine.pkl')

# 2. Configuration InfluxDB (Remplacez par vos vrais identifiants)
INFLUX_URL = "http://localhost:8086"
INFLUX_TOKEN = "my-super-secret-token"
INFLUX_ORG = "beetter"
INFLUX_BUCKET = "sensors"
INFLUX_BUCKET_IA = "data_ia" 

client = InfluxDBClient(url=INFLUX_URL, token=INFLUX_TOKEN, org=INFLUX_ORG)
query_api = client.query_api()

print("🛡️ Démarrage de la surveillance 24/7...")

while True:
    try:
        # A. La bonne requête InfluxDB ! 
        # On attrape toutes les mesures qui commencent par "mfcc_int_"
        query = f"""
        from(bucket: "{INFLUX_BUCKET}")
          |> range(start: -5m)
          |> filter(fn: (r) => r["_measurement"] =~ /^mfcc_int_/)
          |> pivot(rowKey:["_time"], columnKey: ["_measurement"], valueColumn: "_value")
        """
        
        resultats = query_api.query_data_frame(query)
        
        if not resultats.empty:
            # On prend le relevé le plus récent
            derniere_ligne = resultats.iloc[-1]
            beehive_id = derniere_ligne.get("beehive_id", "1")
            
            # On reconstitue l'empreinte de 13 cases dynamiquement
            empreinte = np.array([
                derniere_ligne.get(f"mfcc_int_{i}", 0.0) for i in range(13)
            ]).reshape(1, -1)
            
            # B. Le verdict de l'IA (On prend la case [1] = la certitude du danger)
            certitude_f = modele_f.predict_proba(empreinte)[0][1]
            certitude_r = modele_c.predict_proba(empreinte)[0][1]
            
            # C. Logique de décision
            etat_texte = "normal"
            if certitude_f >= 0.75:
                etat_texte = "attaque_frelon"
            elif certitude_r >= 0.75:
                etat_texte = "chant_reine"

            # D. Écriture immédiate dans le Bucket IA
            write_ia_prediction(
                client=client,
                org=INFLUX_ORG,
                bucket_ia=INFLUX_BUCKET_IA,
                beehive_id=beehive_id,
                etat_detecte=etat_texte,
                proba_f=certitude_f,
                proba_r=certitude_r,
                timestamp=derniere_ligne["_time"]
            )
            
        else:
            print("⏳ Aucune nouvelle donnée reçue de l'ESP32 dans les 5 dernières minutes.")

    except Exception as e:
        print(f"❌ Erreur lors de la vérification : {e}")
    
    # On endort le script pendant 5 minutes (300 secondes)
    time.sleep(300)