"""Logistic regression model for predicting NR handover commands.

Trains a binary classifier on RSRP difference and SINR features extracted
from an O-RAN simulation, then exports the fitted pipeline (scaler + model)
to ONNX format for use in the ns-3 ONNX handover Logic Module.
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from sklearn.model_selection import train_test_split
from sklearn.metrics import classification_report, confusion_matrix
from sklearn.preprocessing import StandardScaler
from imblearn.over_sampling import SMOTE
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import roc_curve, roc_auc_score
from sklearn.pipeline import Pipeline

# Load dataset
# CSV produced by extract_db_to_csv.py from an O-RAN simulation database
df = pd.read_csv("vienna-ho-331-286-extracted-0.1s.csv")

df.shape

# Select input features and the binary target label
X = df[['rsrp_serving_dbm', 'rsrp_neighbor_dbm', 'sinr_serving_db']]
y = df["ho_command_issued"]

# 80/20 train-test split with a fixed seed for reproducibility
X_train, X_test, Y_train, Y_test = train_test_split(X, y, test_size=0.2, random_state=79)

Y_train.value_counts()

# Handle class imbalance with SMOTE
# Handover events are rare (~1-2% of ticks), so we oversample the minority
# class. k_neighbors=1 because there are very few positive samples.
smote_training = SMOTE(random_state=42, k_neighbors=1)
smote_testing = SMOTE(random_state=42, k_neighbors=1)

X_train, Y_train = smote_training.fit_resample(X_train, Y_train)
X_test, Y_test = smote_testing.fit_resample(X_test, Y_test)

# Feature engineering
# Replace the two raw RSRP columns with their difference.
# rsrp_diff = rsrp_serving - rsrp_neighbor captures how much stronger
# the serving cell is; a small or negative value suggests handover is needed.
X_train["rsrp_diff"] = X_train["rsrp_serving_dbm"] - X_train["rsrp_neighbor_dbm"]
X_train.drop(columns=["rsrp_serving_dbm", "rsrp_neighbor_dbm"], inplace=True)

X_test["rsrp_diff"] = X_test["rsrp_serving_dbm"] - X_test["rsrp_neighbor_dbm"]
X_test.drop(columns=["rsrp_serving_dbm", "rsrp_neighbor_dbm"], inplace=True)

# Keep a copy of the test features before fitting (for later inspection)
X_eval = X_test.copy()

# Build the pipeline
# StandardScaler normalizes features, then logistic regression classifies.
# The entire pipeline is exported to ONNX so the ns-3 LM gets identical scaling.
pipeline = Pipeline([('scaler', StandardScaler()),
    ('classifier', LogisticRegression(solver='liblinear', random_state=43))])


def plot_cm(Y_test, preds, Title):
    """Display a confusion matrix heatmap with annotated cell counts."""
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


# Train the model
pipeline.fit(X_train, Y_train)

# Training evaluation
preds_train = pipeline.predict(X_train)

print(classification_report(Y_train, preds_train))

plot_cm(Y_train, preds_train, "Logistic Model (Training)")

# Testing evaluation
preds = pipeline.predict(X_test)

print(classification_report(Y_test, preds))

plot_cm(Y_test, preds, "Logstic Model (Test)")

# Log the learned coefficients for the two features: [sinr_serving_db, rsrp_diff]
pipeline["classifier"].coef_[0]

# ROC curve analysis
# Compare train vs test AUC to check for overfitting
y_train_pred_proba = pipeline["classifier"].predict_proba(X_train)[:, 1]
fpr_train, tpr_train, thresholds_train = roc_curve(Y_train, y_train_pred_proba)
auc_score_train = roc_auc_score(Y_train, y_train_pred_proba)

y_pred_proba = pipeline["classifier"].predict_proba(X_test)[:, 1]
fpr, tpr, thresholds = roc_curve(Y_test, y_pred_proba)
auc_score = roc_auc_score(Y_test, y_pred_proba)

# Plot train and test ROC curves side by side
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

# Show a sample input row (useful for verifying ONNX input shape)
X_train[:1]

# Export to ONNX
# The ONNX file bundles the StandardScaler and LogisticRegression so the
# ns-3 OranLmLte2LteOnnxHandover LM can run inference with raw features.
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
