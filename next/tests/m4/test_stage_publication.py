"""多图共用封存版本，仍由正式Maa和单RunCoordinator执行。"""
import unittest

from next.tests.m4 import test_workflow as workflow_fixture


class StagePublicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        workflow_fixture.WorkflowTests.setUpClass()

    def fixture(self, name, **options):
        runner = workflow_fixture.WorkflowTests()
        return runner.execute(name, [{"Inn": (100, 400)}, {"Stay": (100, 600)},
                                     {"Economy": (100, 500)}],
                              [dict(kind=0, x=120, y=412), dict(kind=0, x=120, y=612)],
                              workflow="staged-publication", with_state=True, **options)

    def test_two_distinct_graphs_share_revision_and_finish_separate_units(self):
        result = self.fixture("stages-normal")
        state = result["snapshot"]
        self.assertEqual(state["state"], "Completed", state["reason"])
        self.assertEqual(state["completed_business_units"], 2)
        self.assertEqual(result["backend_calls"], 2)
        self.assertFalse(result["mismatch"])
        self.assertTrue(state["quiescent"])
        definitions = [s["definition"] for s in state["sessions"]]
        self.assertEqual(len(definitions), 2)
        self.assertEqual([d["entry"] for d in definitions], ["Stage0_Entry", "Stage1_Entry"])
        self.assertEqual(len({d["pack_revision"] for d in definitions}), 1)
        self.assertEqual([s["inputs"]["backend_called"] for s in state["sessions"]], [1, 1])

    def test_missing_later_stage_asset_rejects_before_connection(self):
        result = self.fixture("stages-missing-second", omit_image="Economy.png")
        self.assertEqual(result["publish_error"], "COMPILE_IMAGE_NOT_IN_MANIFEST:image/Economy.png")
        self.assertEqual(result["connections"], 0)
        self.assertEqual(result["backend_calls"], 0)


if __name__ == "__main__":
    unittest.main()
