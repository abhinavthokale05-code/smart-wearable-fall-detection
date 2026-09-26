import pandas as pd

from sklearn.ensemble import RandomForestClassifier

from sklearn.metrics import (
    accuracy_score,
    confusion_matrix,
    classification_report
)

# Load train/test sets
train_df = pd.read_csv("train.csv")
test_df = pd.read_csv("test.csv")

print("Train samples:", len(train_df))
print("Test samples:", len(test_df))

# Target
y_train = train_df["activity_label"]
y_test = test_df["activity_label"]

# Remove non-feature columns
drop_cols = [
    "event_id",
    "source_file",
    "activity_label",
    "algorithm_result",
    "fall_type",
    "ground_truth"
]

X_train = train_df.drop(columns=drop_cols, errors="ignore")
X_test = test_df.drop(columns=drop_cols, errors="ignore")

# Fill blanks
X_train = X_train.fillna(X_train.median(numeric_only=True))
X_test = X_test.fillna(X_train.median(numeric_only=True))

# Train model
rf = RandomForestClassifier(
    n_estimators=35,
    max_depth=10,
    class_weight="balanced",
    random_state=42,
    n_jobs=-1
)

rf.fit(X_train, y_train)

y_prob = rf.predict_proba(X_test)[:,1]

y_pred = (y_prob > 0.40).astype(int)

print("\nAccuracy")
print(accuracy_score(y_test, y_pred))

print("\nConfusion Matrix")
print(confusion_matrix(y_test, y_pred))

print("\nClassification Report")
print(classification_report(y_test, y_pred))

importance = pd.DataFrame({
    "Feature": X_train.columns,
    "Importance": rf.feature_importances_
})

importance = importance.sort_values(
    by="Importance",
    ascending=False
)

print("\nFeature Importance")
print(importance)

import joblib

joblib.dump(
    rf,
    "fall_model_v4.pkl"
)

print("Model saved")