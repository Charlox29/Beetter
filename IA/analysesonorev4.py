import numpy as np
import librosa
from sklearn.ensemble import RandomForestClassifier
from sklearn.model_selection import train_test_split
import random

"""
    MFCC de frelon pour la détection des frelons confirmations
"""

ruche, sr = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/ruche_exterieur.wav", sr=8000)
frelon1, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/frelon1.wav", sr=8000)
frelon2, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/frelon2.wav", sr=8000)
frelon3, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/frelon3.wav", sr=8000)
frelon4, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/frelon4.wav", sr=8000)

# X contiendra nos empreintes (les 13 nombres)
# y contiendra les réponses (0 = Ruche, 1 = Frelon)
X = [] 
Y = [] 

taille_fenetre = 3 * sr  # Fenêtre de 3 secondes
saut = 3 * sr            # On avance de 1 seconde à la fois

frelon = np.concatenate((frelon1, frelon2))
frelon = np.concatenate((frelon, frelon3))
frelon = np.concatenate((frelon, frelon4))

# Découpage mathématique instantané
blocs_ruche = librosa.util.frame(ruche, frame_length=taille_fenetre, hop_length=saut)
blocs_frelon = librosa.util.frame(frelon, frame_length=taille_fenetre, hop_length=saut)

nb_blocs=min(blocs_ruche.shape[1],blocs_frelon.shape[1])

for i in range(nb_blocs):
    morceau = blocs_ruche[:, i]
    mfcc = librosa.feature.mfcc(y=morceau, sr=sr, n_mfcc=13, fmin=50, fmax=3000)
    
    # L'astuce : on fait la moyenne sur l'axe du temps (axis=1)
    mfcc_moyen = np.mean(mfcc, axis=1) 
    
    X.append(mfcc_moyen)
    Y.append(0) # 0 = "Ce n'est pas un frelon"

for i in range(nb_blocs):
    morceau_frelon = blocs_frelon[:, i]
    index_ruche_au_hasard = random.randint(0, blocs_ruche.shape[1] - 1)
    morceau_ruche = blocs_ruche[:, index_ruche_au_hasard]
    volume_f = random.uniform(0.3, 1.5)
    volume_r = 0.5 
    mixage = (morceau_ruche * volume_r) + (morceau_frelon * volume_f)
    mfcc = librosa.feature.mfcc(y=mixage, sr=sr, n_mfcc=13, fmin=50, fmax=3000)
    mfcc_moyen = np.mean(mfcc, axis=1)
    X.append(mfcc_moyen)
    Y.append(1)

# On convertit nos listes en tableaux Numpy pour l'IA
X = np.array(X)
Y = np.array(Y)

print(f"📊 Dataset prêt ! {len(X)} échantillons analysés (13 caractéristiques chacun).")

X_train, X_test, Y_train, Y_test = train_test_split(X, Y, test_size=0.2, random_state=42)

modele_f = RandomForestClassifier(n_estimators=100, random_state=42)

modele_f.fit(X_train, Y_train) 

score = modele_f.score(X_test, Y_test)

print(f"🎯 Précision de l'IA : {score * 100:.2f}%")

"""
    MFCC d'essaimage confirmation des essaimages par audio exterieur
"""

essaimage1, sr = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/essaimage1.wav", sr=8000)
essaimage2, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/essaimage2.wav", sr=8000)

A = [] 
B = [] 

taille_fenetre = 5 * sr  # Fenêtre de 10 secondes
saut = 5 * sr            # On avance de 1 seconde à la fois

essaimage = np.concatenate((essaimage1, essaimage2))

blocs_essaimage = librosa.util.frame(essaimage, frame_length=taille_fenetre, hop_length=saut)

blocs_ruche = librosa.util.frame(ruche, frame_length=taille_fenetre, hop_length=saut)

nb_blocs=min(blocs_ruche.shape[1],blocs_essaimage.shape[1])

for i in range(nb_blocs):
    morceau = blocs_essaimage[:, i]
    mfcc = librosa.feature.mfcc(y=morceau, sr=sr, n_mfcc=13, fmin=50, fmax=3000)
    
    # L'astuce : on fait la moyenne sur l'axe du temps (axis=1)
    mfcc_moyen = np.mean(mfcc, axis=1) 
    A.append(mfcc_moyen)
    B.append(1) # 0 = "Ce n'est pas un frelon"

for i in range(nb_blocs):
    morceau = blocs_ruche[:, i]
    mfcc = librosa.feature.mfcc(y=morceau, sr=sr, n_mfcc=13, fmin=50, fmax=3000)
    mfcc_moyen = np.mean(mfcc, axis=1) 
    A.append(mfcc_moyen)
    B.append(0) # 0 = "Ce n'est pas un frelon"

# On convertit nos listes en tableaux Numpy pour l'IA
A = np.array(A)
B = np.array(B)

print(f"📊 Dataset prêt ! {len(A)} échantillons analysés (13 caractéristiques chacun).")

A_train, A_test, B_train, B_test = train_test_split(A, B, test_size=0.2, random_state=42)

modele_e = RandomForestClassifier(n_estimators=100, random_state=42)

modele_e.fit(A_train, B_train) 

score = modele_e.score(A_test, B_test)

print(f"🎯 Précision de l'IA : {score * 100:.2f}%")

"""
    MFCC de chant de la reine pour confirmation
"""

ruche_int1, sr = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/ruche_interieur1.wav", sr=8000)
ruche_int2, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/ruche_interieur2.wav", sr=8000)
chant1, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/chant2.wav", sr=8000)
chant2, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/chant3.wav", sr=8000)
chant3, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/chant4.wav", sr=8000)
chant4, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/chant5.wav", sr=8000)
chant5, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/chant7.wav", sr=8000)
chant6, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/chant8.wav", sr=8000)
chant7, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/chant9.wav", sr=8000)
chant8, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/chant10.wav", sr=8000)
chant9, _ = librosa.load("C:/Users/simon/Desktop/Esiee/projet fin d'année/fichier sonore frelon asiatique/chant11.wav", sr=8000)

chant_list=[chant1,chant2,chant3,chant4,chant5,chant6,chant7,chant8,chant9]

C = [] 
D = [] 

taille_fenetre = 3 * sr  # Fenêtre de 3 secondes
saut = 3 * sr            # On avance de 1 seconde à la fois

ruche_int=np.concatenate((ruche_int1,ruche_int2))

chant=np.array([])
for ele in chant_list:
    chant=np.concatenate((chant,ele))


# Découpage mathématique instantané
blocs_ruche_int = librosa.util.frame(ruche_int, frame_length=taille_fenetre, hop_length=saut)
blocs_chant = librosa.util.frame(chant, frame_length=taille_fenetre, hop_length=saut)

nb_blocs=min(blocs_ruche_int.shape[1],blocs_chant.shape[1])

for i in range(nb_blocs):
    morceau = blocs_ruche_int[:, i]
    mfcc = librosa.feature.mfcc(y=morceau, sr=sr, n_mfcc=13, fmin=50, fmax=3000)
    mfcc_moyen = np.mean(mfcc, axis=1) 
    C.append(mfcc_moyen)
    D.append(0) # 0 = "Ce n'est pas un frelon"

for i in range(nb_blocs):
    morceau_chant = blocs_chant[:, i]
    index_ruche_au_hasard = random.randint(0, blocs_ruche_int.shape[1] - 1)
    morceau_ruche_int = blocs_ruche_int[:, index_ruche_au_hasard]
    volume_f = random.uniform(0.3, 1.5)
    volume_r = 0.5 
    mixage = (morceau_ruche_int * volume_r) + (morceau_chant * volume_f)
    mfcc = librosa.feature.mfcc(y=mixage, sr=sr, n_mfcc=13, fmin=50, fmax=3000)
    mfcc_moyen = np.mean(mfcc, axis=1)
    C.append(mfcc_moyen)
    D.append(1)

# On convertit nos listes en tableaux Numpy pour l'IA
C = np.array(C)
D = np.array(D)

print(f"📊 Dataset prêt ! {len(C)} échantillons analysés (13 caractéristiques chacun).")

C_train, C_test, D_train, D_test = train_test_split(C, D, test_size=0.2, random_state=42)

modele_c = RandomForestClassifier(n_estimators=100, random_state=42)

modele_c.fit(C_train, D_train) 

score = modele_c.score(C_test, D_test)

print(f"🎯 Précision de l'IA : {score * 100:.2f}%")
