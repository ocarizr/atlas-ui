"""Recover transient artifact-discovery failures without hiding permanent API errors."""

import io
from urllib.error import HTTPError, URLError

import pytest

from scripts.ci import vendor_artifacts


@pytest.fixture
def requests(monkeypatch):
    """Capture authenticated requests and retry delays without network access or sleeping."""
    monkeypatch.setenv("GH_TOKEN", "test-token")
    calls, delays, outcomes = [], [], []

    def open_response(request, timeout):
        """Return the next isolated response or raise its modeled transport error."""
        calls.append((request, timeout))
        outcome = outcomes.pop(0)
        if isinstance(outcome, Exception):
            raise outcome
        return io.StringIO(outcome)

    monkeypatch.setattr(vendor_artifacts, "urlopen", open_response)
    monkeypatch.setattr(vendor_artifacts, "sleep", delays.append, raising=False)
    return calls, delays, outcomes


@pytest.mark.parametrize("status", [429, 500, 502, 503, 504], ids=lambda code: f"http-{code}")
def test_transient_http_error_retries_same_authenticated_request(requests, status):
    """A transient lookup failure can recover without changing the requested artifact identity."""
    calls, delays, outcomes = requests
    outcomes.extend([HTTPError("https://api.github.com/test", status, "temporary", {}, None), "{}"])
    assert vendor_artifacts.github_fetch("/test") == {}
    assert delays == [1]
    assert len(calls) == 2
    for request, timeout in calls:
        assert request.full_url == "https://api.github.com/test"
        assert request.get_header("Authorization") == "Bearer test-token"
        assert timeout == 60


@pytest.mark.parametrize(
    "error",
    [URLError("connection reset"), TimeoutError("timed out")],
    ids=["connection-reset", "timeout"],
)
def test_transport_failure_recovers(requests, error):
    """Temporary transport failures follow the same bounded retry policy."""
    calls, delays, outcomes = requests
    outcomes.extend([error, '{"artifacts": []}'])
    assert vendor_artifacts.github_fetch("/test") == {"artifacts": []}
    assert len(calls) == 2
    assert delays == [1]


def test_transient_failure_exhaustion_preserves_original_error(requests):
    """Persistent service failures stop after four attempts instead of becoming a cache miss."""
    calls, delays, outcomes = requests
    error = HTTPError("https://api.github.com/test", 502, "unavailable", {}, None)
    outcomes.extend([error] * 4)
    with pytest.raises(HTTPError) as caught:
        vendor_artifacts.github_fetch("/test")
    assert caught.value is error
    assert len(calls) == 4
    assert delays == [1, 2, 4]


@pytest.mark.parametrize("status", [400, 401, 403, 404], ids=lambda code: f"http-{code}")
def test_permanent_http_error_is_not_retried(requests, status):
    """Invalid requests, permissions and missing resources fail immediately."""
    calls, delays, outcomes = requests
    outcomes.append(HTTPError("https://api.github.com/test", status, "permanent", {}, None))
    with pytest.raises(HTTPError, match=str(status)):
        vendor_artifacts.github_fetch("/test")
    assert len(calls) == 1
    assert delays == []
