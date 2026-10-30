from pathlib import Path

from scripts.ci.config import read_yaml

ROOT = Path(__file__).resolve().parents[2]


def test_library_jobs_do_not_wait_for_grpc_or_quality():
    workflow = read_yaml(ROOT / '.github/workflows/reusable_validation.yml')
    needs = workflow['jobs']['jobs']['needs']
    assert 'vendor-testing' in needs
    assert 'vendor-ipc' not in needs
    assert 'quality' not in needs


def test_streaming_and_native_checks_can_run_without_ipc():
    jobs = read_yaml(ROOT / '.github/workflows/reusable_validation.yml')['jobs']
    for name in ('streaming', 'windows', 'dependencies'):
        assert 'vendor-ipc' not in jobs[name]['needs']
        assert 'ipc' not in jobs[name]['needs']


def test_component_outputs_are_current_run_profile_artifacts():
    workflow = read_yaml(ROOT / '.github/workflows/reusable_build.yml')
    steps = workflow['jobs']['component']['steps']
    uploads = [step for step in steps if 'upload-artifact@' in step.get('uses', '')]
    assert any('owned-' in step['with']['name'] and 'matrix.configure' in step['with']['name']
               for step in uploads)


def test_only_third_party_products_can_be_looked_up_between_runs():
    workflow = read_yaml(ROOT / '.github/workflows/reusable_dependencies.yml')
    steps = workflow['jobs']['dependencies']['steps']
    lookup = next(step for step in steps if step.get('id') == 'lookup')
    assert 'scripts.ci.vendor_artifacts' in lookup['run']
    upload = next(step for step in steps if 'upload-artifact@' in step.get('uses', '')
                  and step.get('name') == 'Store compiled dependency product')
    assert upload['with']['retention-days'] == 14
