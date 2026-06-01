import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, confusion_matrix
import matplotlib.pyplot as plt

from sklearn.preprocessing import StandardScaler
from imblearn.over_sampling import SMOTE

from sklearn.linear_model import LogisticRegression

from sklearn.metrics import roc_curve, roc_auc_score
from sklearn.pipeline import Pipeline

df = pd.read_csv("vienna-ho-331-286-extracted-0.1s.csv")

df.shape

X = df[['rsrp_serving_dbm', 'rsrp_neighbor_dbm', 'sinr_serving_db']]
y = df["ho_command_issued"]

X_train, X_test, Y_train, Y_test = train_test_split(X, y, test_size=0.2, random_state=79)

Y_train.value_counts()

smote_training = SMOTE(random_state=42, k_neighbors=1)
smote_testing = SMOTE(random_state=42, k_neighbors=1)

X_train, Y_train = smote_training.fit_resample(X_train, Y_train)
X_test, Y_test = smote_testing.fit_resample(X_test, Y_test)

X_train["rsrp_diff"] = X_train["rsrp_serving_dbm"] - X_train["rsrp_neighbor_dbm"]

X_train.drop(columns=["rsrp_serving_dbm", "rsrp_neighbor_dbm"], inplace=True)

X_test["rsrp_diff"] = X_test["rsrp_serving_dbm"] - X_test["rsrp_neighbor_dbm"]

X_test.drop(columns=["rsrp_serving_dbm", "rsrp_neighbor_dbm"], inplace=True)

X_eval = X_test.copy()

pipeline = Pipeline([('scaler', StandardScaler()),
    ('classifier', LogisticRegression(solver='liblinear', random_state=43))])

def plot_cm(Y_test, preds, Title):
  cm_rf = confusion_matrix(Y_test, preds)

  plt.figure(figsize=(6, 5))
  plt.imshow(cm_rf, interpolation='nearest', cmap=plt.cm.Blues)
  plt.title(f'Confusion Matrix - {Title}')
  plt.colorbar()
  tick_marks = np.arange(len(np.unique(Y_test)))
  plt.xticks(tick_marks, np.unique(Y_test), rotation=45)
  plt.yticks(tick_marks, np.unique(Y_test))
  thresh = cm_rf.max() / 2.
  for i in range(cm_rf.shape[0]):
      for j in range(cm_rf.shape[1]):
          plt.text(j, i, format(cm_rf[i, j], 'd'),
                  horizontalalignment="center",
                  color="white" if cm_rf[i, j] > thresh else "black")
  plt.ylabel('True label')
  plt.xlabel('Predicted label')
  plt.tight_layout()
  plt.show()

pipeline.fit(X_train, Y_train)

"""## Training Evaluation"""

preds_train = pipeline.predict(X_train)

print(classification_report(Y_train, preds_train))

plot_cm(Y_train, preds_train, "Logistic Model (Training)")

"""## Testing Evaluation"""

preds = pipeline.predict(X_test)

print(classification_report(Y_test, preds))

plot_cm(Y_test, preds, "Logstic Model (Test)")

pipeline["classifier"].coef_[0]

y_train_pred_proba = pipeline["classifier"].predict_proba(X_train)[:, 1]

fpr_train, tpr_train, thresholds_train = roc_curve(Y_train, y_train_pred_proba)

auc_score_train = roc_auc_score(Y_train, y_train_pred_proba)


y_pred_proba = pipeline["classifier"].predict_proba(X_test)[:, 1]


fpr, tpr, thresholds = roc_curve(Y_test, y_pred_proba)

auc_score = roc_auc_score(Y_test, y_pred_proba)


plt.figure(figsize=(8, 6))

plt.plot(fpr_train, tpr_train, color='green', lw=2, label=f'ROC curve (Train, AUC = {auc_score_train:.2f})')

plt.plot(fpr, tpr, color='blue', lw=2, label=f'ROC curve (Test, AUC = {auc_score:.2f})')

plt.plot([0, 1], [0, 1], color='red', lw=1, linestyle='--', label='Random Classifier')

plt.xlabel('False Positive Rate')
plt.ylabel('True Positive Rate')
plt.title('Receiver Operating Characteristic (ROC) Curve Comparison')
plt.legend(loc="lower right")
plt.grid(True)
plt.show()

X_train[:1]

from skl2onnx import to_onnx

feature_names = ["sinr_serving_db", "rsrp_diff"]

onnx_model = to_onnx(
    pipeline,
    X_train[:1].astype(np.float32),
)

with open("nr_rsrp_sinr_logistic.onnx", "wb") as f:
    f.write(onnx_model.SerializeToString())

print(f"Saved ONNX pipeline (scaler + model) with input features "
      f"{feature_names} to nr_rsrp_sinr_logistic.onnx")