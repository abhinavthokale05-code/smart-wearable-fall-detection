from micromlgen import port
import joblib

model = joblib.load("fall_model_v4.pkl")

with open("rf_model.h", "w") as f:
    f.write(port(model))

print("rf_model.h created successfully")