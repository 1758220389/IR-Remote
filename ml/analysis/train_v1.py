"""Session-held-out V1 training and evaluation. Run from any directory."""
import argparse
from itertools import product
import json
from pathlib import Path
from hashlib import sha256

import joblib
import numpy as np
import pandas as pd
import sklearn
from sklearn.linear_model import LogisticRegression
from sklearn.metrics import classification_report, confusion_matrix
from sklearn.model_selection import LeaveOneGroupOut
from sklearn.pipeline import make_pipeline
from sklearn.preprocessing import StandardScaler
from sklearn.tree import DecisionTreeClassifier

from features import extract

ML = Path(__file__).resolve().parents[1]
LABELS = ["CW", "CCW", "NONE"]
ANGLE = "gyro_integral_z_deg"
PEAK = "gyro_peak_abs_z_dps"
ABS = "gyro_abs_integral_z_deg"
RATIO = "gyro_z_energy_ratio"
MENUS = [(ANGLE,), (ANGLE, PEAK), (ANGLE, PEAK, ABS, RATIO)]


def load(manifest):
    data = pd.read_csv(manifest)
    data = data[data.included].reset_index(drop=True)
    signals = []
    for row in data.itertuples():
        path = ML / row.raw_csv
        if sha256(path.read_bytes()).hexdigest() != row.raw_sha256:
            raise ValueError(f"Manifest hash mismatch: {path}")
        meta = json.loads((ML / row.metadata).read_text(encoding="utf-8"))
        raw = pd.read_csv(path)
        signals.append((raw.t_ms.to_numpy(), raw[["ax_raw", "ay_raw", "az_raw"]].to_numpy()
                        / meta["accel_lsb_per_g"], raw[["gx_raw", "gy_raw", "gz_raw"]].to_numpy()
                        / meta["gyro_lsb_per_dps"]))
    return data, signals


def prepare(data, signals, train, held):
    still = [i for i in train if data.at[i, "stationary"] and data.at[i, "label"] == "NONE"]
    bias = np.concatenate([signals[i][2] for i in still]).mean(axis=0)

    def transform(indices):
        return pd.DataFrame([extract(*signals[i], bias, data.at[i, "duration_ms"])
                             for i in indices], index=indices)

    return transform(train), transform(held), bias


def candidates(kind, duration=False):
    if kind == "A":
        return [(MENUS[0], (left, right)) for left, right in product((4, 8, 12, 16), (3, 4, 6, 10, 14))]
    menus = [(*menu, "duration_ms") for menu in MENUS] if duration else MENUS
    values = (2, 3) if kind == "B" else (.1, 1., 10.)
    return list(product(menus, values))


def fit(kind, config, x, y):
    cols, param = config
    if kind == "A":
        return {"cw_below_deg": -param[0], "ccw_above_deg": param[1]}
    model = (DecisionTreeClassifier(max_depth=param, min_samples_leaf=2, random_state=0)
             if kind == "B" else
             make_pipeline(StandardScaler(), LogisticRegression(C=param, max_iter=1000)))
    return model.fit(x[list(cols)], y)


def predict(kind, model, config, x):
    if kind == "A":
        angle = x[ANGLE]
        return np.where(angle < model["cw_below_deg"], "CW",
                        np.where(angle > model["ccw_above_deg"], "CCW", "NONE"))
    return model.predict(x[list(config[0])])


def errors(y, pred):
    none = (y == "NONE") & (pred != "NONE")
    reversed_ = ((y == "CW") & (pred == "CCW")) | ((y == "CCW") & (pred == "CW"))
    return int((y != pred).sum()), int(none.sum()), int(reversed_.sum())


def select(kind, inner, labels, duration=False):
    ranked = []
    for config in candidates(kind, duration):
        totals = np.zeros(3, dtype=int)
        for train, valid in inner:
            model = fit(kind, config, train, labels[train.index])
            totals += errors(labels[valid.index], predict(kind, model, config, valid))
        ranked.append((tuple(map(int, totals)), config))
    best_error = min(score[0] for score, _ in ranked)
    eligible = [(score, cfg) for score, cfg in ranked if score[0] <= best_error + 1]
    return min(eligible, key=lambda item: (item[0][1], item[0][2], item[0][0],
                                          len(item[1][0]), item[1][1]))


def complexity(kind, model, cols):
    detail = {"features": list(cols), "feature_count": len(cols)}
    if kind == "A":
        detail.update(parameter_count=2, **model)
    elif kind == "B":
        detail.update(tree_depth=model.get_depth(), tree_nodes=model.tree_.node_count)
    else:
        lr = model[-1]
        detail.update(parameter_count=int(lr.coef_.size + lr.intercept_.size),
                      scaler_parameter_count=2 * len(cols))
    return detail


def evaluate(predictions, output):
    scores, matrices, reports = [], [], {}
    main = predictions[predictions.scope.eq("main")]
    for name, all_folds in main.groupby("model"):
        for fold, group in [*all_folds.groupby("fold"), ("ALL", all_folds)]:
            y, pred = group.label.to_numpy(), group.prediction.to_numpy()
            count, false_trigger, reversed_ = errors(y, pred)
            metrics = classification_report(y, pred, labels=LABELS, output_dict=True, zero_division=0)
            report = dict(model=name, fold=fold, samples=len(y), accuracy=1-count/len(y),
                          none_false_trigger=false_trigger,
                          none_false_trigger_rate=false_trigger/int((y=="NONE").sum()),
                          cw_missed_as_none=int(((y=="CW") & (pred=="NONE")).sum()),
                          ccw_missed_as_none=int(((y=="CCW") & (pred=="NONE")).sum()),
                          cw_reversed=int(((y=="CW") & (pred=="CCW")).sum()),
                          ccw_reversed=int(((y=="CCW") & (pred=="CW")).sum()),
                          reversed_total=reversed_)
            for label in [*LABELS, "macro avg"]:
                for metric in ("precision", "recall", "f1-score"):
                    report[f"{label}_{metric}"] = metrics[label][metric]
            scores.append(report)
            cm = confusion_matrix(y, pred, labels=LABELS)
            matrices.extend(dict(model=name, fold=fold, actual=a, predicted=b, count=cm[i,j])
                            for i,a in enumerate(LABELS) for j,b in enumerate(LABELS))
            reports[f"{name}/{fold}"] = metrics
    pd.DataFrame(scores).to_csv(output / "metrics.csv", index=False)
    pd.DataFrame(matrices).to_csv(output / "confusion.csv", index=False)
    (output / "classification_reports.json").write_text(json.dumps(reports, indent=2), encoding="utf-8")
    extra = predictions[predictions.scope.eq("supplemental")]
    pd.DataFrame([dict(model=model, fold=fold, samples=len(g), correct=g.prediction.eq("CW").sum(),
                       missed=g.prediction.eq("NONE").sum(), reversed=g.prediction.eq("CCW").sum())
                  for (model, fold), g in extra.groupby(["model","fold"])]).to_csv(output / "supplemental.csv", index=False)


def export_recommended(out, data, signals, primary, extra, splitter):
    labels = data.label.to_numpy()
    groups = data.session.to_numpy()[primary]
    inner = [prepare(data, signals, primary[a], primary[b])[:2]
             for a, b in splitter.split(primary, groups=groups)]
    score, config = select("C", inner, labels)
    x, held, bias = prepare(data, signals, primary, extra)
    pipeline = fit("C", config, x, labels[primary])
    scaler, lr = pipeline.steps[0][1], pipeline.steps[1][1]
    weights = lr.coef_ / scaler.scale_
    intercept = lr.intercept_ - weights @ scaler.mean_
    check = pd.concat([x, held])[list(config[0])]
    fused_guess = lr.classes_[np.argmax(check.to_numpy(dtype=np.float32)
                  @ weights.astype(np.float32).T + intercept.astype(np.float32), axis=1)]
    if not np.array_equal(fused_guess, pipeline.predict(check)):
        raise RuntimeError("Float32 fused scores disagree with the PC model")
    guess = predict("C", pipeline, config, held)
    details = dict(model="C", purpose="refit after evaluation", training_sessions=sorted(set(groups)),
                   bias_dps=bias.tolist(), classes=lr.classes_.tolist(), C=config[1],
                   inner_errors=score, weights=weights.tolist(), intercept=intercept.tolist(),
                   supplemental_correct=int((guess == "CW").sum()),
                   supplemental_samples=len(extra), **complexity("C", pipeline, config[0]))
    joblib.dump({"model": pipeline, "details": details}, out / "recommended_C.joblib")
    (out / "recommended_C.json").write_text(json.dumps(details, indent=2), encoding="utf-8")
    data.loc[extra, ["batch_id", "session", "label"]].assign(prediction=guess).to_csv(
        out / "recommended_C_supplemental.csv", index=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ML / "data/v1/manifest.csv")
    parser.add_argument("--output", type=Path, default=ML / "models/v1")
    args = parser.parse_args()
    out = args.output.resolve()
    if out.is_relative_to((ML / "data/real").resolve()):
        parser.error("output must be outside the raw capture tree")
    data, signals = load(args.manifest)
    labels = data.label.to_numpy()
    primary = data.index[data.scope.eq("complete_guided")].to_numpy()
    extra = data.index[data.scope.eq("supplemental_manual")].to_numpy()
    sessions = data.session.to_numpy()
    if len(primary) != 120 or len(set(sessions[primary])) != 4 or len(extra) != 10:
        parser.error("expected the accepted V1 snapshot: 120 main and 10 supplemental windows")
    for _, group in data.loc[primary].groupby("session"):
        if group.groupby(["label", "stationary"]).size().to_dict() != {
            ("CW", False): 10, ("CCW", False): 10, ("NONE", True): 5, ("NONE", False): 5
        }:
            parser.error("expected 10 CW, 10 CCW, 5 static NONE and 5 motion NONE per session")
    out.mkdir(parents=True, exist_ok=True)
    predictions, settings = [], []
    splitter = LeaveOneGroupOut()
    for it, iv in splitter.split(primary, groups=sessions[primary]):
        train, held = primary[it], primary[iv]
        fold = sessions[held[0]]
        x, evaluation, bias = prepare(data, signals, train, np.r_[held, extra])
        inner = [prepare(data, signals, train[a], train[b])[:2]
                 for a,b in splitter.split(train, groups=sessions[train])]
        for kind, duration in (("A",False),("B",False),("C",False),("B",True),("C",True)):
            name = kind + ("_duration" if duration else "")
            score, config = select(kind, inner, labels, duration)
            model = fit(kind, config, x, labels[train])
            pred = predict(kind, model, config, evaluation)
            details = dict(model=name, fold=fold, training_sessions=sorted(set(sessions[train])),
                           bias_dps=bias.tolist(), inner_errors=score, setting=config[1],
                           **complexity(kind, model, config[0]))
            joblib.dump({"model": model, "details": details}, out / f"{name}__{fold}.joblib")
            settings.append(details)
            for row_index, guess in zip(evaluation.index, pred):
                row = data.loc[row_index]
                v = evaluation.loc[row_index]
                predictions.append(dict(model=name, fold=fold, scope="main" if row_index in held else "supplemental",
                                        batch_id=row.batch_id, session=row.session, label=row.label,
                                        stationary=row.stationary, prediction=guess, raw_csv=row.raw_csv,
                                        **v.to_dict()))
    predictions = pd.DataFrame(predictions)
    predictions.to_csv(out / "predictions.csv", index=False)
    predictions[predictions.label.ne(predictions.prediction)].to_csv(out / "errors.csv", index=False)
    evaluate(predictions, out)
    (out / "fold_models.json").write_text(json.dumps(settings, indent=2), encoding="utf-8")
    export_recommended(out, data, signals, primary, extra, splitter)
    run = dict(manifest_sha256=sha256(args.manifest.read_bytes()).hexdigest(),
               sklearn=sklearn.__version__, numpy=np.__version__, pandas=pd.__version__)
    (out / "run.json").write_text(json.dumps(run, indent=2), encoding="utf-8")
    scores = pd.read_csv(out / "metrics.csv")
    print(scores[scores.fold.eq("ALL")][["model","accuracy","none_false_trigger","reversed_total"]].to_string(index=False))


if __name__ == "__main__":
    main()
