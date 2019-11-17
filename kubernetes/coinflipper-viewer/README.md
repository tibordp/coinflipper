# Coin Flipper Viewer

This is a simple web frontend for the Coin Flipper that you can deploy alongside your Coin Flipper deployment.

It uses `nginx` to serve the files and a `coinflipper` sidecar that periodically refreshes the status and saves it to file.

## Instructions for deployment

Modify the `manifest.yaml` and any static assets to suit your needs (especially the ingress part).

The static assets are stored as a ConfigMap, so upload them to.
```
kubectl create configmap coinflipper-viewer --from-file=assets/ -o yaml --dry-run | kubectl apply -f -
```

Then deploy the manifest
```
kubectl apply -f manifest.yaml
```