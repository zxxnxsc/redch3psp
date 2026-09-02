# GTA III PSP CI game-build blocker

The PSPSDK container successfully builds and packages PSP EBOOTs. The remaining game-build blocker is the reconstruction of the full PSP engine target (PSP platform Makefile/bootstrap plus the custom librw PSP backend used by build 9Y).

A previous CI attempt also exposed a container-only tooling issue: the `pspdev/pspdev:latest` image does not include `python3`, while `tools/bootstrap-upstream.sh` currently uses the deterministic Python source transformer. The workflow is being adjusted so source reconstruction happens on the Ubuntu runner and the prepared tree is passed into the PSPDEV build job.
