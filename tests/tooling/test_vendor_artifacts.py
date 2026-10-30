import pytest

from scripts.ci.vendor_artifacts import find_product


@pytest.fixture
def catalog():
    artifacts = [dict(id=1, name='vendor-ipc-key', expired=False, workflow_run={'id': 10})]
    run = dict(head_branch='main', event='push', path='.github/workflows/delivery.yml',
               head_repository={'full_name': 'owner/repo'})
    jobs = {'jobs': [dict(name='vendor-ipc / vendor-ipc', status='completed', conclusion='success')]}
    def fetch(path):
        if '/artifacts?' in path:
            return {'artifacts': artifacts} if 'page=1&' in path else {'artifacts': []}
        return jobs if '/jobs?' in path else run
    return artifacts, run, jobs, fetch


def test_reuses_successful_main_producer_even_if_unrelated_checks_are_running(catalog):
    _, run, _, fetch = catalog
    run['status'] = 'in_progress'
    assert find_product(fetch, 'owner/repo', 'ipc', 'key', 20) == 10


@pytest.mark.parametrize('mutation', ['expired', 'pull-request', 'wrong-repository', 'failed-job',
                                    'wrong-workflow', 'same-run', 'wrong-key'])
def test_does_not_reuse_untrusted_or_incompatible_products(catalog, mutation):
    artifacts, run, jobs, fetch = catalog
    if mutation == 'expired':
        artifacts[0]['expired'] = True
    elif mutation == 'pull-request':
        run['event'] = 'pull_request'
    elif mutation == 'wrong-repository':
        run['head_repository']['full_name'] = 'fork/repo'
    elif mutation == 'failed-job':
        jobs['jobs'][0]['conclusion'] = 'failure'
    elif mutation == 'wrong-workflow':
        run['path'] = '.github/workflows/unrelated.yml'
    elif mutation == 'same-run':
        artifacts[0]['workflow_run']['id'] = 20
    elif mutation == 'wrong-key':
        artifacts[0]['name'] = 'vendor-ipc-other-key'
    assert find_product(fetch, 'owner/repo', 'ipc', 'key', 20) is None


def test_searches_later_catalog_pages(catalog):
    _, run, jobs, _ = catalog
    def fetch(path):
        if '/artifacts?' in path:
            if 'page=1&' in path:
                return {'artifacts': [dict(id=index, name='other', expired=True) for index in range(100)]}
            return {'artifacts': [dict(id=101, name='vendor-ipc-key', expired=False,
                                       workflow_run={'id': 10})]}
        return jobs if '/jobs?' in path else run
    assert find_product(fetch, 'owner/repo', 'ipc', 'key', 20) == 10


def test_searches_later_producer_job_pages(catalog):
    artifacts, run, jobs, _ = catalog
    def fetch(path):
        if '/artifacts?' in path:
            return {'artifacts': artifacts}
        if '/jobs?' in path:
            if 'page=2' not in path:
                return {'jobs': [dict(name='unrelated', status='completed', conclusion='success')
                                 for _ in range(100)]}
            return jobs
        return run
    assert find_product(fetch, 'owner/repo', 'ipc', 'key', 20) == 10
