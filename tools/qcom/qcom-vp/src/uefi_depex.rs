//! Decode UEFI dependency expressions for the platform map.

use anyhow::{bail, ensure, Context, Result};
use serde::Serialize;
use std::collections::{BTreeMap, BTreeSet};

const OP_BEFORE: u8 = 0x00;
const OP_AFTER: u8 = 0x01;
const OP_PUSH: u8 = 0x02;
const OP_AND: u8 = 0x03;
const OP_OR: u8 = 0x04;
const OP_NOT: u8 = 0x05;
const OP_TRUE: u8 = 0x06;
const OP_FALSE: u8 = 0x07;
const OP_END: u8 = 0x08;
const OP_SOR: u8 = 0x09;

/// A decoded UEFI dependency expression.
#[derive(Clone, Debug, PartialEq, Eq, Serialize)]
pub enum Expr {
    True,
    False,
    Guid(String),
    And(Box<Expr>, Box<Expr>),
    Or(Box<Expr>, Box<Expr>),
    Not(Box<Expr>),
    Before(String),
    After(String),
    Sor(Box<Expr>),
}

/// The direction of a BEFORE or AFTER constraint.
#[derive(Clone, Copy, Debug, PartialEq, Eq, Serialize)]
pub enum SchedulingOrder {
    Before,
    After,
}

/// Format 16 EFI GUID bytes in lowercase canonical form.
///
/// The first three fields use little-endian order.
pub fn guid(bytes: &[u8]) -> Result<String> {
    ensure!(
        bytes.len() == 16,
        "The GUID has {} bytes instead of 16",
        bytes.len()
    );
    Ok(format!(
        "{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        bytes[3],
        bytes[2],
        bytes[1],
        bytes[0],
        bytes[5],
        bytes[4],
        bytes[7],
        bytes[6],
        bytes[8],
        bytes[9],
        bytes[10],
        bytes[11],
        bytes[12],
        bytes[13],
        bytes[14],
        bytes[15],
    ))
}

/// Decode the payload of a dependency section.
///
/// The decoder removes trailing zero bytes from section padding.
/// A missing END opcode, an empty stack, an extra value, or an operand
/// underflow is an error.
pub fn decode(bytes: &[u8]) -> Result<Expr> {
    let end = bytes
        .iter()
        .rposition(|byte| *byte != 0x00)
        .map_or(0, |position| position + 1);
    let bytes = &bytes[..end];
    ensure!(!bytes.is_empty(), "The dependency expression is empty");

    let mut cursor = 0;
    let mut sor = false;
    if bytes[cursor] == OP_SOR {
        sor = true;
        cursor += 1;
        ensure!(cursor < bytes.len(), "The SOR opcode has no expression");
    }

    let mut stack: Vec<Expr> = Vec::new();
    loop {
        let opcode = *bytes
            .get(cursor)
            .context("The dependency expression has no END opcode")?;
        cursor += 1;
        match opcode {
            OP_BEFORE | OP_AFTER => {
                ensure!(
                    cursor == 1 && !sor,
                    "BEFORE or AFTER must start the dependency expression"
                );
                let value = read_guid(bytes, &mut cursor)?;
                ensure!(
                    bytes.get(cursor) == Some(&OP_END) && cursor + 1 == bytes.len(),
                    "BEFORE or AFTER must contain one file GUID followed by END"
                );
                stack.push(if opcode == OP_BEFORE {
                    Expr::Before(value)
                } else {
                    Expr::After(value)
                });
            }
            OP_PUSH => stack.push(Expr::Guid(read_guid(bytes, &mut cursor)?)),
            OP_AND => {
                let right = pop_operand(&mut stack, "AND")?;
                let left = pop_operand(&mut stack, "AND")?;
                stack.push(Expr::And(Box::new(left), Box::new(right)));
            }
            OP_OR => {
                let right = pop_operand(&mut stack, "OR")?;
                let left = pop_operand(&mut stack, "OR")?;
                stack.push(Expr::Or(Box::new(left), Box::new(right)));
            }
            OP_NOT => {
                let value = pop_operand(&mut stack, "NOT")?;
                stack.push(Expr::Not(Box::new(value)));
            }
            OP_TRUE => stack.push(Expr::True),
            OP_FALSE => stack.push(Expr::False),
            OP_END => {
                ensure!(
                    stack.len() == 1,
                    "The dependency expression leaves {} values on the stack",
                    stack.len()
                );
                ensure!(
                    cursor == bytes.len(),
                    "The dependency expression has {} bytes after END",
                    bytes.len() - cursor
                );
                let value = stack.pop().expect("The stack holds one value");
                return Ok(if sor {
                    Expr::Sor(Box::new(value))
                } else {
                    value
                });
            }
            OP_SOR => bail!("The SOR opcode is not the first byte"),
            _ => bail!("The dependency expression has an unknown opcode {opcode:#04x}"),
        }
    }
}

fn read_guid(bytes: &[u8], cursor: &mut usize) -> Result<String> {
    let end = *cursor + 16;
    ensure!(
        end <= bytes.len(),
        "The dependency expression ends inside a GUID"
    );
    let value = guid(&bytes[*cursor..end])?;
    *cursor = end;
    Ok(value)
}

fn pop_operand(stack: &mut Vec<Expr>, operation: &str) -> Result<Expr> {
    stack
        .pop()
        .with_context(|| format!("The {operation} opcode has too few operands"))
}

impl Expr {
    /// Return the GUIDs in sorted order. Remove duplicates.
    pub fn guids(&self) -> Vec<String> {
        let mut values = Vec::new();
        self.collect_guids(&mut values);
        values.sort();
        values.dedup();
        values
    }

    fn collect_guids(&self, values: &mut Vec<String>) {
        match self {
            Expr::Guid(value) | Expr::Before(value) | Expr::After(value) => {
                values.push(value.clone());
            }
            Expr::And(left, right) | Expr::Or(left, right) => {
                left.collect_guids(values);
                right.collect_guids(values);
            }
            Expr::Not(value) | Expr::Sor(value) => value.collect_guids(values),
            Expr::True | Expr::False => {}
        }
    }

    /// Return the BEFORE and AFTER constraints in source order.
    pub fn scheduling(&self) -> Vec<(SchedulingOrder, String)> {
        let mut values = Vec::new();
        self.collect_scheduling(&mut values);
        values
    }

    fn collect_scheduling(&self, values: &mut Vec<(SchedulingOrder, String)>) {
        match self {
            Expr::Before(value) => values.push((SchedulingOrder::Before, value.clone())),
            Expr::After(value) => values.push((SchedulingOrder::After, value.clone())),
            Expr::And(left, right) | Expr::Or(left, right) => {
                left.collect_scheduling(values);
                right.collect_scheduling(values);
            }
            Expr::Not(value) | Expr::Sor(value) => value.collect_scheduling(values),
            Expr::True | Expr::False | Expr::Guid(_) => {}
        }
    }

    /// Return true when a SOR opcode applies to the expression.
    pub fn has_sor(&self) -> bool {
        match self {
            Expr::Sor(_) => true,
            Expr::And(left, right) | Expr::Or(left, right) => left.has_sor() || right.has_sor(),
            Expr::Not(value) => value.has_sor(),
            Expr::True | Expr::False | Expr::Guid(_) | Expr::Before(_) | Expr::After(_) => false,
        }
    }

    /// Evaluate the expression against the installed protocol GUIDs.
    ///
    /// BEFORE and AFTER nodes evaluate to true.
    /// The caller applies their scheduling constraints.
    pub fn evaluate(&self, available: &BTreeSet<String>) -> bool {
        match self {
            Expr::True => true,
            Expr::False => false,
            Expr::Guid(value) => available.contains(value),
            Expr::And(left, right) => left.evaluate(available) && right.evaluate(available),
            Expr::Or(left, right) => left.evaluate(available) || right.evaluate(available),
            Expr::Not(value) => !value.evaluate(available),
            Expr::Before(_) | Expr::After(_) => true,
            Expr::Sor(value) => value.evaluate(available),
        }
    }

    /// Render the expression and resolve each GUID name.
    ///
    /// An unknown GUID keeps its canonical text.
    pub fn display(&self, names: &BTreeMap<String, String>) -> String {
        match self {
            Expr::True => "TRUE".to_owned(),
            Expr::False => "FALSE".to_owned(),
            Expr::Guid(value) => format!("PUSH {}", label(value, names)),
            Expr::And(left, right) => {
                format!("({} AND {})", left.display(names), right.display(names))
            }
            Expr::Or(left, right) => {
                format!("({} OR {})", left.display(names), right.display(names))
            }
            Expr::Not(value) => format!("(NOT {})", value.display(names)),
            Expr::Before(value) => format!("BEFORE {}", label(value, names)),
            Expr::After(value) => format!("AFTER {}", label(value, names)),
            Expr::Sor(value) => format!("SOR {}", value.display(names)),
        }
    }
}

fn label(value: &str, names: &BTreeMap<String, String>) -> String {
    names
        .get(value)
        .cloned()
        .unwrap_or_else(|| value.to_owned())
}

#[cfg(test)]
mod tests {
    use super::*;

    const GUID_A: [u8; 16] = [
        0x61, 0xdf, 0xe4, 0x8b, 0xca, 0x93, 0xd2, 0x11, 0xaa, 0x0d, 0x00, 0xe0, 0x98, 0x03, 0x2b,
        0x8c,
    ];
    const GUID_A_TEXT: &str = "8be4df61-93ca-11d2-aa0d-00e098032b8c";
    const GUID_B: [u8; 16] = [
        0xa1, 0x31, 0x1b, 0x5b, 0x62, 0x95, 0xd2, 0x11, 0x8e, 0x3f, 0x00, 0xa0, 0xc9, 0x69, 0x72,
        0x3b,
    ];
    const GUID_B_TEXT: &str = "5b1b31a1-9562-11d2-8e3f-00a0c969723b";

    fn op(opcode: u8) -> Vec<u8> {
        vec![opcode]
    }

    fn push(raw: &[u8; 16]) -> Vec<u8> {
        let mut bytes = vec![OP_PUSH];
        bytes.extend_from_slice(raw);
        bytes
    }

    fn bytecode(parts: &[Vec<u8>]) -> Vec<u8> {
        parts.iter().flatten().copied().collect()
    }

    fn decode_parts(parts: &[Vec<u8>]) -> Expr {
        decode(&bytecode(parts)).expect("The dependency expression must decode")
    }

    fn plain(expr: &Expr) -> String {
        expr.display(&BTreeMap::new())
    }

    fn named(expr: &Expr) -> String {
        let names = BTreeMap::from([
            (GUID_A_TEXT.to_owned(), "gEfiGlobalVariableGuid".to_owned()),
            (
                GUID_B_TEXT.to_owned(),
                "gEfiLoadedImageProtocolGuid".to_owned(),
            ),
        ]);
        expr.display(&names)
    }

    #[test]
    fn guid_uses_mixed_endian_order() {
        let raw = [
            0x78, 0x56, 0x34, 0x12, 0xbc, 0x9a, 0xf0, 0xde, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66,
            0x77, 0x88,
        ];
        assert_eq!(guid(&raw).unwrap(), "12345678-9abc-def0-1122-334455667788");
    }

    #[test]
    fn guid_renders_known_protocol_identifier() {
        assert_eq!(guid(&GUID_B).unwrap(), GUID_B_TEXT);
    }

    #[test]
    fn guid_rejects_wrong_length() {
        assert!(guid(&[]).is_err());
        assert!(guid(&[0; 15]).is_err());
        assert!(guid(&[0; 17]).is_err());
    }

    #[test]
    fn constant_opcodes_decode() {
        assert_eq!(plain(&decode(&[OP_TRUE, OP_END]).unwrap()), "TRUE");
        assert_eq!(plain(&decode(&[OP_FALSE, OP_END]).unwrap()), "FALSE");
    }

    #[test]
    fn push_decodes_protocol_guid() {
        let expr = decode_parts(&[push(&GUID_A), op(OP_END)]);
        assert_eq!(expr, Expr::Guid(GUID_A_TEXT.to_owned()));
        assert_eq!(plain(&expr), format!("PUSH {GUID_A_TEXT}"));
        assert_eq!(named(&expr), "PUSH gEfiGlobalVariableGuid");
    }

    #[test]
    fn not_opcode_inverts_operand() {
        let expr = decode_parts(&[push(&GUID_A), op(OP_NOT), op(OP_END)]);
        assert_eq!(
            expr,
            Expr::Not(Box::new(Expr::Guid(GUID_A_TEXT.to_owned())))
        );
        assert_eq!(plain(&expr), format!("(NOT PUSH {GUID_A_TEXT})"));
        assert!(expr.evaluate(&BTreeSet::new()));
        assert!(!expr.evaluate(&BTreeSet::from([GUID_A_TEXT.to_owned()])));
    }

    #[test]
    fn and_opcode_keeps_operand_order() {
        let expr = decode_parts(&[push(&GUID_A), op(OP_TRUE), op(OP_AND), op(OP_END)]);
        assert_eq!(named(&expr), "(PUSH gEfiGlobalVariableGuid AND TRUE)");
    }

    #[test]
    fn or_opcode_keeps_operand_order() {
        let expr = decode_parts(&[push(&GUID_A), op(OP_FALSE), op(OP_OR), op(OP_END)]);
        assert_eq!(named(&expr), "(PUSH gEfiGlobalVariableGuid OR FALSE)");
    }

    #[test]
    fn nested_operators_keep_precedence() {
        let expr = decode_parts(&[
            push(&GUID_A),
            push(&GUID_B),
            op(OP_OR),
            op(OP_NOT),
            op(OP_TRUE),
            op(OP_AND),
            op(OP_END),
        ]);
        assert_eq!(
            plain(&expr),
            format!("((NOT (PUSH {GUID_A_TEXT} OR PUSH {GUID_B_TEXT})) AND TRUE)")
        );
    }

    #[test]
    fn sor_wraps_the_expression() {
        let expr = decode_parts(&[op(OP_SOR), push(&GUID_A), op(OP_END)]);
        assert_eq!(
            expr,
            Expr::Sor(Box::new(Expr::Guid(GUID_A_TEXT.to_owned())))
        );
        assert_eq!(plain(&expr), format!("SOR PUSH {GUID_A_TEXT}"));
        assert!(expr.has_sor());
        assert!(expr.evaluate(&BTreeSet::from([GUID_A_TEXT.to_owned()])));
        assert!(!expr.evaluate(&BTreeSet::new()));
        assert!(!Expr::Guid(GUID_A_TEXT.to_owned()).has_sor());
    }

    #[test]
    fn before_opcode_records_scheduling() {
        let expr = decode_parts(&[op(OP_BEFORE), GUID_A.to_vec(), op(OP_END)]);
        assert_eq!(expr, Expr::Before(GUID_A_TEXT.to_owned()));
        assert_eq!(plain(&expr), format!("BEFORE {GUID_A_TEXT}"));
        assert_eq!(
            expr.scheduling(),
            vec![(SchedulingOrder::Before, GUID_A_TEXT.to_owned())]
        );
        assert_eq!(expr.guids(), vec![GUID_A_TEXT.to_owned()]);
        assert!(expr.evaluate(&BTreeSet::new()));
    }

    #[test]
    fn after_opcode_records_scheduling() {
        let expr = decode_parts(&[op(OP_AFTER), GUID_B.to_vec(), op(OP_END)]);
        assert_eq!(named(&expr), "AFTER gEfiLoadedImageProtocolGuid");
        assert_eq!(
            expr.scheduling(),
            vec![(SchedulingOrder::After, GUID_B_TEXT.to_owned())]
        );
        assert!(expr.evaluate(&BTreeSet::new()));
    }

    #[test]
    fn guids_are_sorted_and_unique() {
        let expr = decode_parts(&[
            push(&GUID_A),
            push(&GUID_B),
            op(OP_AND),
            push(&GUID_A),
            op(OP_OR),
            op(OP_END),
        ]);
        assert_eq!(
            expr.guids(),
            vec![GUID_B_TEXT.to_owned(), GUID_A_TEXT.to_owned()]
        );
    }

    #[test]
    fn scheduling_rejects_boolean_and_request_operands() {
        for opcode in [OP_BEFORE, OP_AFTER] {
            assert!(decode(&bytecode(&[
                op(OP_SOR),
                op(opcode),
                GUID_A.to_vec(),
                op(OP_END),
            ]))
            .is_err());
            assert!(decode(&bytecode(&[
                op(opcode),
                GUID_A.to_vec(),
                op(OP_NOT),
                op(OP_END),
            ]))
            .is_err());
            assert!(decode(&bytecode(&[
                op(OP_TRUE),
                op(opcode),
                GUID_A.to_vec(),
                op(OP_AND),
                op(OP_END),
            ]))
            .is_err());
        }
    }

    #[test]
    fn evaluate_requires_all_installed_protocols() {
        let expr = decode_parts(&[push(&GUID_A), push(&GUID_B), op(OP_AND), op(OP_END)]);
        assert!(!expr.evaluate(&BTreeSet::new()));
        assert!(!expr.evaluate(&BTreeSet::from([GUID_A_TEXT.to_owned()])));
        assert!(expr.evaluate(&BTreeSet::from([
            GUID_A_TEXT.to_owned(),
            GUID_B_TEXT.to_owned(),
        ])));
    }

    #[test]
    fn evaluate_combines_or_and_not() {
        let expr = decode_parts(&[
            push(&GUID_A),
            op(OP_NOT),
            push(&GUID_B),
            op(OP_OR),
            op(OP_END),
        ]);
        assert!(expr.evaluate(&BTreeSet::new()));
        assert!(expr.evaluate(&BTreeSet::from([GUID_B_TEXT.to_owned()])));
        assert!(!expr.evaluate(&BTreeSet::from([GUID_A_TEXT.to_owned()])));
    }

    #[test]
    fn display_covers_every_node() {
        let expr = Expr::And(
            Box::new(Expr::Or(
                Box::new(Expr::Guid(GUID_A_TEXT.to_owned())),
                Box::new(Expr::Not(Box::new(Expr::After(GUID_B_TEXT.to_owned())))),
            )),
            Box::new(Expr::Sor(Box::new(Expr::True))),
        );
        assert_eq!(
            named(&expr),
            "((PUSH gEfiGlobalVariableGuid OR (NOT AFTER gEfiLoadedImageProtocolGuid)) AND SOR TRUE)"
        );
        assert_eq!(plain(&Expr::False), "FALSE");
        assert_eq!(
            plain(&Expr::Before(GUID_A_TEXT.to_owned())),
            format!("BEFORE {GUID_A_TEXT}")
        );
    }

    #[test]
    fn section_padding_is_ignored() {
        let mut bytes = bytecode(&[push(&GUID_A), op(OP_END)]);
        bytes.extend_from_slice(&[0x00; 3]);
        assert_eq!(decode(&bytes).unwrap(), Expr::Guid(GUID_A_TEXT.to_owned()));
    }

    #[test]
    fn trailing_guid_zero_is_not_padding() {
        let mut raw = GUID_A;
        raw[15] = 0x00;
        let bytes = bytecode(&[op(OP_PUSH), raw.to_vec(), op(OP_END)]);
        let text = guid(&raw).unwrap();
        assert!(text.ends_with("00"));
        assert_eq!(decode(&bytes).unwrap(), Expr::Guid(text));
    }

    #[test]
    fn serialization_keeps_expression_shape() {
        let expr = decode_parts(&[push(&GUID_A), op(OP_NOT), op(OP_END)]);
        assert_eq!(
            serde_json::to_value(&expr).unwrap(),
            serde_json::json!({"Not": {"Guid": GUID_A_TEXT}})
        );
        assert_eq!(
            serde_json::to_value(SchedulingOrder::Before).unwrap(),
            serde_json::json!("Before")
        );
    }

    #[test]
    fn empty_or_zero_payload_is_rejected() {
        assert!(decode(&[]).is_err());
        assert!(decode(&[0x00; 4]).is_err());
    }

    #[test]
    fn truncated_guid_is_rejected() {
        let mut bytes = push(&GUID_A);
        bytes.pop();
        bytes.push(OP_END);
        assert!(decode(&bytes).is_err());

        let mut bytes = vec![OP_BEFORE];
        bytes.extend_from_slice(&GUID_A[..15]);
        bytes.push(OP_END);
        assert!(decode(&bytes).is_err());
    }

    #[test]
    fn missing_end_is_rejected() {
        assert!(decode(&[OP_TRUE]).is_err());
        assert!(decode(&push(&GUID_A)).is_err());
        assert!(decode(&bytecode(&[op(OP_SOR), op(OP_TRUE)])).is_err());
    }

    #[test]
    fn end_requires_one_value() {
        assert!(decode(&[OP_END]).is_err());
        assert!(decode(&[OP_TRUE, OP_FALSE, OP_END]).is_err());
        assert!(decode(&bytecode(&[
            op(OP_SOR),
            op(OP_TRUE),
            op(OP_FALSE),
            op(OP_END)
        ]))
        .is_err());
    }

    #[test]
    fn trailing_bytes_after_end_are_rejected() {
        assert!(decode(&[OP_TRUE, OP_END, OP_FALSE]).is_err());
    }

    #[test]
    fn operator_underflow_is_rejected() {
        assert!(decode(&[OP_AND, OP_END]).is_err());
        assert!(decode(&[OP_TRUE, OP_AND, OP_END]).is_err());
        assert!(decode(&[OP_OR, OP_END]).is_err());
        assert!(decode(&[OP_TRUE, OP_OR, OP_END]).is_err());
        assert!(decode(&[OP_NOT, OP_END]).is_err());
    }

    #[test]
    fn unknown_opcode_is_rejected() {
        for opcode in [0x0a_u8, 0x0f, 0x10, 0x7f, 0xff] {
            assert!(decode(&[opcode, OP_END]).is_err());
            assert!(decode(&bytecode(&[op(OP_TRUE), op(opcode), op(OP_END)])).is_err());
        }
    }

    #[test]
    fn sor_must_be_the_first_byte() {
        assert!(decode(&[OP_TRUE, OP_SOR, OP_END]).is_err());
        assert!(decode(&bytecode(&[
            op(OP_SOR),
            op(OP_SOR),
            op(OP_TRUE),
            op(OP_END)
        ]))
        .is_err());
        assert!(decode(&bytecode(&[
            op(OP_PUSH),
            GUID_A.to_vec(),
            op(OP_SOR),
            op(OP_END)
        ]))
        .is_err());
    }

    #[test]
    fn sor_requires_an_expression() {
        assert!(decode(&[OP_SOR]).is_err());
        assert!(decode(&[OP_SOR, OP_END]).is_err());
        assert!(decode(&[OP_SOR, 0x00]).is_err());
    }
}
