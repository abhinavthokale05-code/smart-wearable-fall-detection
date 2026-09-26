import pandas as pd
from sklearn.model_selection import train_test_split

# Load full dataset
df = pd.read_csv("fall_detection_dataset_v4.csv")

print("Total samples:", len(df))
print("\nClass distribution:")
print(df["activity_label"].value_counts())

# Create stratified split
train_df, test_df = train_test_split(
    df,
    test_size=0.20,          # 80% train, 20% test
    random_state=42,
    stratify=df["activity_label"]
)

# Save files
train_df.to_csv("train.csv", index=False)
test_df.to_csv("test.csv", index=False)

print("\nTrain samples:", len(train_df))
print("Test samples:", len(test_df))

print("\nTrain distribution:")
print(train_df["activity_label"].value_counts())

print("\nTest distribution:")
print(test_df["activity_label"].value_counts())

print("\nFiles created:")
print("train.csv")
print("test.csv")